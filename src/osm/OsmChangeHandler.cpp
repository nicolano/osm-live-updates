// Copyright 2024, University of Freiburg
// Authors: Nicolas von Trott <nicolasvontrott@gmail.com>.

// This file is part of osm-live-updates.
//
// osm-live-updates is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// osm-live-updates is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with osm-live-updates.  If not, see <https://www.gnu.org/licenses/>.

#include "osm/OsmChangeHandler.h"
#include "util/XmlReader.h"
#include "util/Decompressor.h"
#include "config/Constants.h"
#include "sparql/QueryWriter.h"
#include "util/OsmObjectHelper.h"
#include "util/TtlHelper.h"

#include <boost/property_tree/ptree.hpp>
#include <string>
#include <iostream>
#include <set>
#include <regex>

// The maximum number of values that should be in a query to the QLever endpoint.
static inline constexpr int MAX_VALUES_PER_QUERY = 1024;
// Calculate number of ids for delete queries, because there are for example two subjects
// for each node id
static inline constexpr int MAX_IDS_PER_NODE_DELETE_QUERY_BATCH = MAX_VALUES_PER_QUERY / 2;
static inline constexpr int MAX_IDS_PER_WAY_DELETE_QUERY_BATCH = MAX_VALUES_PER_QUERY / 3;
static inline constexpr int MAX_IDS_PER_REL_DELETE_QUERY_BATCH = MAX_VALUES_PER_QUERY / 2;

namespace cnst = olu::config::constants;

void doInBatches(std::set<olu::id_t>& set, const long elementsPerBatch,
                 const std::function<void(std::set<olu::id_t>)>& func) {
    std::vector vector(set.begin(), set.end());
    std::vector<std::set<olu::id_t> > vectorBatches;
    for (auto it = vector.cbegin(), e = vector.cend(); it != vector.cend(); it = e) {
        e = it + std::min<std::size_t>(vector.end() - it, elementsPerBatch);
        vectorBatches.emplace_back(it, e);
    }

    for (const auto &vectorBatch: vectorBatches) {
        func(vectorBatch);
    }
}

namespace olu::osm {
    OsmChangeHandler::OsmChangeHandler(const config::Config &config,
                                       const std::string &pathToOsmChangeFile) : _config(config),
                                                                                 _sparql(config),
                                                                                 _odf(config) {
        try {
            if (pathToOsmChangeFile.ends_with(cnst::GZIP_EXTENSION)) {
                const auto decompressed = util::Decompressor::readGzip(pathToOsmChangeFile);
                util::XmlReader::populatePTreeFromString(decompressed, _osmChangeElement);
            } else {
                util::XmlReader::populatePTreeFromFile(pathToOsmChangeFile, _osmChangeElement);
            }

            _osmChangeElement = _osmChangeElement.get_child(cnst::OSM_CHANGE_TAG);
        } catch (std::exception &e) {
            std::cerr << e.what() << std::endl;
            throw OsmChangeHandlerException(
                "Exception while trying to read the change file in a property tree");
        }

        createOrClearTmpFiles();
    }

    void OsmChangeHandler::run() {
        std::cout << "Process change file..." << std::endl;
        // Store the ids of all elements that where deleted, modified or created and the ids of
        // objects where the geometry needs to be updated
        storeIdsOfElementsInChangeFile();
        processElementsInChangeFile();
        getIdsOfWaysToUpdateGeo();
        getIdsOfRelationsToUpdateGeo();

        std::cout << "Fetch references..." << std::endl;
        // Get the ids of all referenced objects
//        getReferencedRelations(); Skipped atm because osm2rdf does not calculate the geometry for
//                                  relations that reference other relations
        getReferencesForRelations();
        getReferencesForWays();

        std::cout << "Create dummy objects..." << std::endl;
        // Create dummy objects for the referenced osm objects
        createDummyNodes();
        createDummyWays();
        createDummyRelations();

        std::cout << "Convert data..." << std::endl;
        // Convert osm objects to triples
        try {
            Osm2ttl::convert();
        } catch (std::exception &e) {
            std::cerr << e.what() << std::endl;
            throw OsmChangeHandlerException(
                "Exception while trying to convert osm element to ttl");
        }

        std::cout << "Update database..." << std::endl;
        // Delete and insert elements from database
        deleteNodesFromDatabase();
        deleteWaysFromDatabase();
        deleteRelationsFromDatabase();
        insertTriplesToDatabase();

        // Cache of sparql endpoint has to be cleared after the completion`
        _sparql.clearCache();

        std::cout << "nodes created: " << _createdNodes.size() << " modified: "
                << _modifiedNodes.size() << " deleted: " << _deletedNodes.size() << std::endl;
        std::cout << "ways created: " << _createdWays.size() << " modified: "
                << _modifiedWays.size() << " deleted: " << _deletedWays.size() << std::endl;
        std::cout << "relations created: " << _createdRelations.size() << " modified: "
                << _modifiedRelations.size() << " deleted: " << _deletedRelations.size() <<
                std::endl;
        std::cout << "updated geometries for " << _waysToUpdateGeometry.size() << " ways "
                << _relationsToUpdateGeometry.size() << " relations" << std::endl;
    }

    void OsmChangeHandler::createOrClearTmpFiles() {
        initTmpFile(cnst::PATH_TO_NODE_FILE);
        initTmpFile(cnst::PATH_TO_WAY_FILE);
        initTmpFile(cnst::PATH_TO_RELATION_FILE);

        std::ofstream file4(cnst::PATH_TO_TRIPLES_FILE, std::ios::trunc);
        file4.close();
    }

    void OsmChangeHandler::initTmpFile(const std::string& filepath) {
        std::ofstream file(filepath, std::ios::trunc);
        file << "<osm version=\"0.6\">" << std::endl;
        file.close();
    }

    void OsmChangeHandler::finalizeTmpFile(const std::string& filepath) {
        std::ofstream file(filepath, std::ios::app);
        file << "</osm>" << std::endl;
        file.close();
    }

    void OsmChangeHandler::addToTmpFile(const std::string& element,
                                        const std::string& elementTag) {
        std::ofstream outputFile;
        if (elementTag == cnst::NODE_TAG) {
            outputFile.open (cnst::PATH_TO_NODE_FILE, std::ios::app);
        } else if (elementTag == cnst::WAY_TAG) {
            outputFile.open (cnst::PATH_TO_WAY_FILE, std::ios::app);
        } else if (elementTag == cnst::RELATION_TAG) {
            outputFile.open (cnst::PATH_TO_RELATION_FILE, std::ios::app);
        }

        outputFile << element << std::endl;

        outputFile.close();
    }

    void OsmChangeHandler::storeIdsOfElementsInChangeFile() {
        for (const auto &[changesetTag, changesetElement] : _osmChangeElement) {
            if (changesetTag == "<xmlattr>") { continue; }

            for (const auto &[elementTag, element]: changesetElement) {
                auto id = getIdFor(element);

                if (changesetTag == cnst::MODIFY_TAG) {
                    if (elementTag == cnst::NODE_TAG) {
                        _modifiedNodes.insert(id);
                    } else if (elementTag == cnst::WAY_TAG) {
                        _modifiedWays.insert(id);
                    } else if (elementTag == cnst::RELATION_TAG) {
                        _modifiedRelations.insert(id);

                        if (OsmObjectHelper::isMultipolygon(element)) {
                            _modifiedAreas.insert(id);
                        }
                    }
                } else if (changesetTag == cnst::CREATE_TAG) {
                    if (elementTag == cnst::NODE_TAG) {
                        _createdNodes.insert(id);
                    } else if (elementTag == cnst::WAY_TAG) {
                        _createdWays.insert(id);
                    } else if (elementTag == cnst::RELATION_TAG) {
                        _createdRelations.insert(id);
                    }
                } else if (changesetTag == cnst::DELETE_TAG) {
                    if (elementTag == cnst::NODE_TAG) {
                        _deletedNodes.insert(id);
                    } else if (elementTag == cnst::WAY_TAG) {
                        _deletedWays.insert(id);
                    } else if (elementTag == cnst::RELATION_TAG) {
                        _deletedRelations.insert(id);
                    }
                }
            }
        }
    }

    void OsmChangeHandler::processElementsInChangeFile() {
        for (const auto &[changesetTag, changesetElement]: _osmChangeElement) {
            if (changesetTag == cnst::MODIFY_TAG || changesetTag == cnst::CREATE_TAG) {
                for (const auto &[elementTag, element]: changesetElement) {
                    if (elementTag == cnst::WAY_TAG || elementTag == cnst::RELATION_TAG) {
                        storeIdsOfReferencedElements(element);
                    }

                    addToTmpFile(util::XmlReader::readTree(element, elementTag), elementTag);
                }
            }
        }

        // We do not need to keep the tree in memory after we are done here
        _osmChangeElement.clear();
    }

    void OsmChangeHandler::getIdsOfWaysToUpdateGeo() {
        if (!_modifiedNodes.empty()) {
            doInBatches(
                _modifiedNodes,
                MAX_VALUES_PER_QUERY,
                [this](const std::set<id_t> &batch) {
                    for (const auto &wayId: _odf.fetchWaysReferencingNodes(batch)) {
                        if (!wayInChangeFile(wayId)) {
                            _waysToUpdateGeometry.insert(wayId);
                        }
                    }
                });
        }
    }

    void OsmChangeHandler::getIdsOfRelationsToUpdateGeo() {
        // Get ids of relations that reference a modified node
        if (!_modifiedNodes.empty()) {
            doInBatches(
                _modifiedNodes,
                MAX_VALUES_PER_QUERY,
                [this](const std::set<id_t>& batch) {
                    for (const auto &relId: _odf.fetchRelationsReferencingNodes(batch)) {
                        if (!relationInChangeFile(relId)) {
                            _relationsToUpdateGeometry.insert(relId);
                        }
                    }
                });
        }

        // Get ids of relations that reference a modified way
        std::set<id_t> updatedWays;
        updatedWays.insert(_modifiedWays.begin(), _modifiedWays.end());
        updatedWays.insert(_waysToUpdateGeometry.begin(), _waysToUpdateGeometry.end());
        if (!updatedWays.empty()) {
            doInBatches(
                updatedWays,
                MAX_VALUES_PER_QUERY,
                [this](const std::set<id_t>& batch) {
                    for (const auto &relId: _odf.fetchRelationsReferencingWays(batch)) {
                        if (!relationInChangeFile(relId)) {
                            _relationsToUpdateGeometry.insert(relId);
                        }
                    }
                });
        }

        // Get ids of relations that reference a modified relation. Skip this because
        // osm2rdf does not calculate geometries for relations that reference other relations
//        if (!_modifiedAreas.empty()) {
//            doInBatches(
//                    _modifiedAreas,
//            MAX_TRIPLE_COUNT_PER_QUERY,
//            [this](const std::set<id_t>& batch) {
//                auto relationIds = _odf.fetchRelationsReferencingRelations(batch);
//                for (const auto &relId: relationIds) {
//                    if (!_modifiedAreas.contains(relId)) {
//                        _relationsToUpdateGeometry.insert(relId);
//                    }
//                }
//            });
//        }
    }

    void OsmChangeHandler::getReferencedRelations() {
        if (!_relationsToUpdateGeometry.empty()) {
            doInBatches(
                    _relationsToUpdateGeometry,
                MAX_VALUES_PER_QUERY,
                [this](const std::set<id_t>& batch) {
                    auto relationIds = _odf.fetchRelationsReferencingRelations(batch);
                    for (const auto &relId: relationIds) {
                        if (!_relationsToUpdateGeometry.contains(relId) &&
                            !_createdRelations.contains(relId) &&
                            !_modifiedAreas.contains(relId)) {
                            _referencedRelations.insert(relId);
                        }
                    }
                });
        }
    }

    void OsmChangeHandler::getReferencesForRelations() {
        std::set<id_t> relations;
        relations.insert(_referencedRelations.begin(), _referencedRelations.end());
        relations.insert(_relationsToUpdateGeometry.begin(), _relationsToUpdateGeometry.end());
        if (!relations.empty()) {
            doInBatches(
                    relations,
                    MAX_VALUES_PER_QUERY,
                    [this](const std::set<id_t>& batch) {
                    auto [nodeIds, wayIds] = _odf.fetchRelationMembers(batch);
                    for (const auto &wayId: wayIds) {
                        if (!_waysToUpdateGeometry.contains(wayId) &&
                            !_createdWays.contains(wayId) &&
                            !_modifiedWays.contains(wayId)) {
                            _referencedWays.insert(wayId);
                        }
                    }
                    for (const auto &nodeId: nodeIds) {
                        if (!nodeInChangeFile(nodeId)) {
                            _referencedNodes.insert(nodeId);
                        }
                    }
                });
        }
    }

    void OsmChangeHandler::getReferencesForWays() {
        std::set<id_t> waysToFetchNodesFor;
        waysToFetchNodesFor.insert(_referencedWays.begin(), _referencedWays.end());
        waysToFetchNodesFor.insert(_waysToUpdateGeometry.begin(), _waysToUpdateGeometry.end());
        if (!waysToFetchNodesFor.empty()) {
            doInBatches(
            waysToFetchNodesFor,
            MAX_VALUES_PER_QUERY,
            [this](const std::set<id_t>& batch) {
                for (const auto &nodeId: _odf.fetchWaysMembers(batch)) {
                    if (!nodeInChangeFile(nodeId)) {
                        _referencedNodes.insert(nodeId);
                    }
                }
            });
        }
    }

    void OsmChangeHandler::createDummyNodes() {
        doInBatches(
            _referencedNodes,
            MAX_VALUES_PER_QUERY,
            [this](std::set<id_t> const& batch) {
                for (auto const& node: _odf.fetchNodes(batch)) {
                    addToTmpFile(node.getXml(), cnst::NODE_TAG);
                }
            });

        finalizeTmpFile(cnst::PATH_TO_NODE_FILE);
    }

    void OsmChangeHandler::createDummyWays() {
        std::set<id_t> wayIds;
        wayIds.insert(_referencedWays.begin(), _referencedWays.end());
        wayIds.insert(_waysToUpdateGeometry.begin(), _waysToUpdateGeometry.end());

        doInBatches(
            wayIds,
            MAX_VALUES_PER_QUERY,
            [this](std::set<id_t> const& batch) {
                for (auto& way: _odf.fetchWays(batch)) {
                    if (_waysToUpdateGeometry.contains(way.getId())) {
                        _odf.fetchWayInfos(way);
                    }

                    addToTmpFile(way.getXml(), cnst::WAY_TAG);
                }
            });

        finalizeTmpFile(cnst::PATH_TO_WAY_FILE);
    }

    void OsmChangeHandler::createDummyRelations() {
        std::set<id_t> relations;
        relations.insert(_referencedRelations.begin(), _referencedRelations.end());
        relations.insert(_relationsToUpdateGeometry.begin(), _relationsToUpdateGeometry.end());

        doInBatches(
            relations,
            MAX_VALUES_PER_QUERY,
            [this](std::set<id_t> const& batch) {
                for (auto& rel: _odf.fetchRelations(batch)) {
                    if (_relationsToUpdateGeometry.contains(rel.getId())) {
                        _odf.fetchRelationInfos(rel);
                    }

                    addToTmpFile(rel.getXml(), cnst::RELATION_TAG);
                }
            });

        finalizeTmpFile(cnst::PATH_TO_RELATION_FILE);
    }

    void
    OsmChangeHandler::runUpdateQuery(const std::string &query,
                                     const std::vector<std::string> &prefixes) {
        _sparql.setQuery(query);
        _sparql.setPrefixes(prefixes);
        try {
            _sparql.runUpdate();
        } catch (std::exception &e) {
            std::cerr << e.what() << std::endl;
            const std::string msg = "Exception while trying to run sparql update query: "
                                    + query.substr(0, std::min<int>(query.size(), 100))
                                    + " ...";
            throw OsmChangeHandlerException(msg.c_str());
        }
    }

    void OsmChangeHandler::deleteNodesFromDatabase() {
        std::set<id_t> nodesToDelete;
        nodesToDelete.insert(_deletedNodes.begin(), _deletedNodes.end());
        nodesToDelete.insert(_modifiedNodes.begin(), _modifiedNodes.end());

        doInBatches(
            nodesToDelete,
            MAX_IDS_PER_NODE_DELETE_QUERY_BATCH,
            [this](std::set<id_t> const& batch) {
                runUpdateQuery(sparql::QueryWriter::writeDeleteQuery(batch, "osmnode"),
                    cnst::PREFIXES_FOR_NODE_DELETE_QUERY);
            });
    }

    void OsmChangeHandler::deleteWaysFromDatabase() {
        std::set<id_t> waysToDelete;
        waysToDelete.insert(_deletedWays.begin(), _deletedWays.end());
        waysToDelete.insert(_modifiedWays.begin(), _modifiedWays.end());
        waysToDelete.insert(_waysToUpdateGeometry.begin(), _waysToUpdateGeometry.end());

        doInBatches(
            waysToDelete,
            MAX_IDS_PER_WAY_DELETE_QUERY_BATCH,
            [this](std::set<id_t> const& batch) {
                runUpdateQuery(sparql::QueryWriter::writeDeleteQuery(batch, "osmway"),
                    cnst::PREFIXES_FOR_WAY_DELETE_QUERY);
            });
    }

    void OsmChangeHandler::deleteRelationsFromDatabase() {
        std::set<id_t> relationsToDelete;
        relationsToDelete.insert(_deletedRelations.begin(), _deletedRelations.end());
        relationsToDelete.insert(_modifiedRelations.begin(), _modifiedRelations.end());
        relationsToDelete.insert(_relationsToUpdateGeometry.begin(), _relationsToUpdateGeometry.end());

        doInBatches(
            relationsToDelete,
            MAX_IDS_PER_REL_DELETE_QUERY_BATCH,
            [this](std::set<id_t> const& batch) {
                runUpdateQuery(sparql::QueryWriter::writeDeleteQuery(batch, "osmrel"),
                    cnst::PREFIXES_FOR_RELATION_DELETE_QUERY);
            });
    }

    void OsmChangeHandler::insertTriplesToDatabase() {
        auto triples = filterRelevantTriples();

        std::vector<std::string> tripleBatch;
        for (size_t i = 0; i < triples.size(); ++i) {
            auto [s, p, o] = triples[i];
            std::ostringstream triple;
            if (o.starts_with("_")) {
                triple << s;
                triple << " ";
                triple << p;
                triple << "[ ";

                while (true) {
                    i++;
                    if (auto [next_s, next_p, next_o] = triples[i]; next_s.starts_with("_")) {
                        triple << next_p;
                        triple << " ";
                        triple << next_o;
                        triple << "; ";
                    } else {
                        i--;
                        break;
                    }
                }

                triple << " ]";
            } else {
                triple << s;
                triple << " ";
                triple << p;
                triple << " ";
                triple << o;
            }

            tripleBatch.emplace_back(triple.str());

            if (tripleBatch.size() == MAX_VALUES_PER_QUERY || i == triples.size() - 1) {
                runUpdateQuery(sparql::QueryWriter::writeInsertQuery(tripleBatch), cnst::DEFAULT_PREFIXES);
                tripleBatch.clear();
            }
        }
    }

    std::vector<Triple> OsmChangeHandler::filterRelevantTriples() {
        std::set<id_t> nodesToInsert;
        nodesToInsert.insert(_createdNodes.begin(), _createdNodes.end());
        nodesToInsert.insert(_modifiedNodes.begin(), _modifiedNodes.end());

        std::set<id_t> waysToInsert;
        waysToInsert.insert(_createdWays.begin(), _createdWays.end());
        waysToInsert.insert(_modifiedWays.begin(), _modifiedWays.end());
        waysToInsert.insert(_waysToUpdateGeometry.begin(), _waysToUpdateGeometry.end());

        std::set<id_t> relationsToInsert;
        relationsToInsert.insert(_createdRelations.begin(), _createdRelations.end());
        relationsToInsert.insert(_modifiedRelations.begin(), _modifiedRelations.end());
        relationsToInsert.insert(_relationsToUpdateGeometry.begin(), _relationsToUpdateGeometry.end());

        // Triples that should be inserted into the database
        std::vector<Triple> relevantTriples;
        // current link object, for example member nodes or geometries
        std::string currentLink;

        // Loop over each triple that osm2rdf outputs
        std::string line;
        std::ifstream osm2rdfOutput;
        osm2rdfOutput.open(cnst::PATH_TO_OUTPUT_FILE);
        while (std::getline(osm2rdfOutput, line)) {
            if (line.starts_with("@")) {
                continue;
            }

            auto [sub, pre, obj] = util::TtlHelper::getTriple(line);

            // Check if there is currently a link set
            if (!currentLink.empty() && currentLink == sub) {
                relevantTriples.emplace_back(sub, pre, obj);
                continue;
            }

            // Check for relevant nodes
            if (util::TtlHelper::isRelevantNamespace(sub, cnst::NODE_TAG)) {
                if (nodesToInsert.contains(util::TtlHelper::getIdFromSubject(sub, cnst::NODE_TAG))) {

                    relevantTriples.emplace_back(sub, pre, obj);

                    if (util::TtlHelper::hasRelevantObject(pre, cnst::NODE_TAG)) {
                        currentLink = obj;
                    }
                }

                continue;
            }

            // Check for relevant ways
            if (util::TtlHelper::isRelevantNamespace(sub, cnst::WAY_TAG)) {
                if (waysToInsert.contains(util::TtlHelper::getIdFromSubject(sub, cnst::WAY_TAG))) {
                    relevantTriples.emplace_back(sub, pre, obj);

                    if (util::TtlHelper::hasRelevantObject(pre, cnst::WAY_TAG)) {
                        currentLink = obj;
                    }
                }

                continue;
            }

            // Check for relevant relations
            if (util::TtlHelper::isRelevantNamespace(sub, cnst::RELATION_TAG)) {
                if (relationsToInsert.contains(util::TtlHelper::getIdFromSubject(sub, cnst::RELATION_TAG))) {
                    relevantTriples.emplace_back(sub, pre, obj);

                    if (util::TtlHelper::hasRelevantObject(pre, cnst::RELATION_TAG)) {
                        currentLink = obj;
                    }
                }
            }
        }

        osm2rdfOutput.close();
        return relevantTriples;
    }

    void
    OsmChangeHandler::storeIdsOfReferencedElements(const boost::property_tree::ptree& relElement) {
        for (const auto &[memberTag, element] : relElement.get_child("")) {
            if (memberTag != "member" && memberTag != "nd") {
                continue;
            }

            std::string type;
            if (memberTag == "nd") {
                type = "node";
            } else {
                type = util::XmlReader::readAttribute("<xmlattr>.type", element);
            }

            auto refIdAsString = util::XmlReader::readAttribute("<xmlattr>.ref", element);

            id_t id;
            try {
                id = std::stoll(refIdAsString);
            } catch(std::exception &e) {
                std::cerr << e.what() << std::endl;
                const std::string msg = "Exception while trying to convert id string: "
                                        + refIdAsString + " to long";
                throw OsmChangeHandlerException(msg.c_str());
            }

            if (type == "node") {
                if (!nodeInChangeFile(id)) {
                    _referencedNodes.insert(id);
                }
            } else if (type == "way") {
                if (!wayInChangeFile(id)) {
                    _referencedWays.insert(id);
                }
            } else if (type == "relation") {
                if (!relationInChangeFile(id)) {
                    _referencedRelations.insert(id);
                }
            } else {
                const std::string msg = "Cant handle member type: " + type + " for element: "
                                        + util::XmlReader::readTree(relElement);
                throw OsmChangeHandlerException(msg.c_str());
            }
        }
    }

    id_t OsmChangeHandler::getIdFor(const boost::property_tree::ptree &element) {
        std::string identifier;
        try {
            identifier = util::XmlReader::readAttribute(config::constants::ID_ATTRIBUTE, element);
        } catch (std::exception &e) {
            std::cerr << e.what() << std::endl;
            const std::string msg = "Could not extract identifier from element: "
                                    + util::XmlReader::readTree(element);
            throw OsmDataFetcherException(msg.c_str());
        }

        try {
            return std::stoll(identifier);
        } catch (std::exception &e) {
            std::cerr << e.what() << std::endl;
            const std::string msg = "Could not cast identifier: " + identifier + " to id_t";
            throw OsmDataFetcherException(msg.c_str());
        }
    }

} // namespace olu::osm
