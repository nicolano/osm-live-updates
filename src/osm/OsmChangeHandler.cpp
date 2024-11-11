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

#include <boost/property_tree/ptree.hpp>
#include <string>
#include <iostream>
#include <set>
#include <regex>

/// The maximum number of triples that can be in a query to the QLever endpoint.
const static inline int MAX_TRIPLE_COUNT_PER_QUERY = 64;
const static inline int DELETE_TRIPLES_PER_WAY = 3;
const static inline int DELETE_TRIPLES_PER_NODE = 2;
const static inline int DELETE_TRIPLES_PER_RELATION = 2;

namespace cnst = olu::config::constants;

void doInBatches(const std::set<long long>& set, int elementsPerBatch, const std::function<void(std::set<long long>)>& func) {
    std::vector vector(set.begin(), set.end());
    std::vector<std::set<long long>> vectorBatches;
    for (auto it = vector.cbegin(), e = vector.cend(); it != vector.cend(); it = e) {
        e = it + std::min<std::size_t>(vector.end() - it, elementsPerBatch);
        vectorBatches.emplace_back(it, e);
    }

    for (auto & vectorBatch : vectorBatches) {
        func(vectorBatch);
    }
}

namespace olu::osm {
    OsmChangeHandler::OsmChangeHandler(config::Config &config,
                                       const std::string &pathToOsmChangeFile) : _config(config),
                                                                                 _sparql(config),
                                                                                 _osm2ttl(),
                                                                                 _odf(OsmDataFetcher(
                                                                                         config)) {
        try {
            if (pathToOsmChangeFile.ends_with(config::constants::GZIP_EXTENSION)) {
                auto decompressed = olu::util::Decompressor::readGzip(pathToOsmChangeFile);
                olu::util::XmlReader::populatePTreeFromString(
                        decompressed,
                        _osmChangeElement);
            } else {
                olu::util::XmlReader::populatePTreeFromFile(pathToOsmChangeFile,
                                                            _osmChangeElement);
            }

            _osmChangeElement = _osmChangeElement.get_child(config::constants::OSM_CHANGE_TAG);
        } catch (std::exception &e) {
            std::cerr << e.what() << std::endl;
            throw OsmChangeHandlerException(
                    "Exception while trying to read the change file in a property tree");
        }

        createOrClearTmpFiles();
    }

    void OsmChangeHandler::run() {
        processChangeFile();
        getIdsForGeometryUpdate();

        getReferencedRelations();
        getReferencedWays();
        getReferencedNodes();

        // Create dummy objects of the referenced osm objects. This is needed for the conversion of
        // the osm elements to rdf format, for the calculation of geometries.
        createDummyNodes();
        createDummyWays();
        createDummyRelations();

        sortFile(cnst::NODE_TAG);
        sortFile(cnst::WAY_TAG);
        sortFile(cnst::RELATION_TAG);

        try {
            _osm2ttl.convert();
        } catch (std::exception &e) {
            std::cerr << e.what() << std::endl;
            throw OsmChangeHandlerException("Exception while trying to convert osm element to ttl");
        }

        deleteElementsFromDatabase();
        insertElementsToDatabase();

//        std::cout << "Nodes to insert: " << _createdNodes.size() << std::endl;
//        std::cout << "Nodes to delete: " << _deletedNodes.size() << std::endl;
//        std::cout << "Nodes to modify: " << _modifiedNodes.size() << std::endl;
//
//        std::cout << "Ways to insert: " << _createdWays.size() << std::endl;
//        std::cout << "Ways to delete: " << _deletedWays.size() << std::endl;
//        std::cout << "Ways to modify: " << _modifiedWays.size() << std::endl;
//        std::cout << "Ways to update geo: " << _waysToUpdateGeometry.size() << std::endl;
//
//        std::cout << "Relations to insert: " << _createdRelations.size() << std::endl;
//        std::cout << "Relations to delete: " << _deletedRelations.size() << std::endl;
//        std::cout << "Relations to modify: " << _modifiedRelations.size() << std::endl;
//        std::cout << "Relations to update geo: " << _relationsToUpdateGeometry.size() << std::endl;

        std::cout << "Done" << std::endl;
    }

    void OsmChangeHandler::createOrClearTmpFiles() {
        std::ofstream file1(cnst::PATH_TO_NODE_FILE, std::ios::trunc);
        file1.close();

        std::ofstream file2(cnst::PATH_TO_WAY_FILE, std::ios::trunc);
        file2.close();

        std::ofstream file3(cnst::PATH_TO_RELATION_FILE, std::ios::trunc);
        file3.close();

        std::ofstream file4(cnst::PATH_TO_TRIPLES_FILE, std::ios::trunc);
        file4.close();
    }

    void OsmChangeHandler::addToTmpFile(const boost::property_tree::ptree& element,
                                        const std::string& elementTag) {
        std::ofstream outputFile;
        if (elementTag == cnst::NODE_TAG) {
            outputFile.open (cnst::PATH_TO_NODE_FILE, std::ios::app);
        } else if (elementTag == cnst::WAY_TAG) {
            outputFile.open (cnst::PATH_TO_WAY_FILE, std::ios::app);
        } else if (elementTag == cnst::RELATION_TAG) {
            outputFile.open (cnst::PATH_TO_RELATION_FILE, std::ios::app);
        }

        outputFile << util::XmlReader::readTree(element, elementTag) << std::endl;

        outputFile.close();
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

    void OsmChangeHandler::processChangeFile() {
        storeIdsOfElementsInChangeFile();
        processElementsInChangeFile();
    }

    void OsmChangeHandler::storeIdsOfElementsInChangeFile() {
        for (const auto &changeset : _osmChangeElement) {
            if (changeset.first == config::constants::MODIFY_TAG) {
                for (const auto &osmElement: changeset.second) {
                    auto id = olu::osm::OsmDataFetcher::getIdFor(osmElement.second);
                    if (osmElement.first == config::constants::NODE_TAG) {
                        _modifiedNodes.insert(id);
                    } else if (osmElement.first == config::constants::WAY_TAG) {
                        _modifiedWays.insert(id);
                    } else if (osmElement.first == config::constants::RELATION_TAG) {
                        _modifiedRelations.insert(id);
                    }
                }
            } else if (changeset.first == config::constants::CREATE_TAG) {
                for (const auto &osmElement: changeset.second) {
                    auto id = olu::osm::OsmDataFetcher::getIdFor(osmElement.second);
                    if (osmElement.first == config::constants::NODE_TAG) {
                        _createdNodes.insert(id);
                    } else if (osmElement.first == config::constants::WAY_TAG) {
                        _createdWays.insert(id);
                    } else if (osmElement.first == config::constants::RELATION_TAG) {
                        _createdRelations.insert(id);
                    }
                }
            } else if (changeset.first == config::constants::DELETE_TAG) {
                for (const auto &osmElement: changeset.second) {
                    auto id = olu::osm::OsmDataFetcher::getIdFor(osmElement.second);
                    if (osmElement.first == config::constants::NODE_TAG) {
                        _deletedNodes.insert(id);
                    } else if (osmElement.first == config::constants::WAY_TAG) {
                        _deletedWays.insert(id);
                    } else if (osmElement.first == config::constants::RELATION_TAG) {
                        _deletedRelations.insert(id);
                    }
                }
            }
        }
    }

    void OsmChangeHandler::processElementsInChangeFile() {
        for (const auto &changeset : _osmChangeElement) {
            if (changeset.first == config::constants::MODIFY_TAG) {
                for (const auto &osmElement: changeset.second) {
                    if (osmElement.first == config::constants::NODE_TAG) {
                        addToTmpFile(osmElement.second, cnst::NODE_TAG);
                    } else if (osmElement.first == config::constants::WAY_TAG) {
                        storeIdsOfReferencedNodes(osmElement.second);
                        addToTmpFile(osmElement.second, cnst::WAY_TAG);
                    } else if (osmElement.first == config::constants::RELATION_TAG) {
                        storeIdsOfReferencedElements(osmElement.second);
                        addToTmpFile(osmElement.second, cnst::RELATION_TAG);
                    }
                }
            } else if (changeset.first == config::constants::CREATE_TAG) {
                for (const auto &osmElement: changeset.second) {
                    if (osmElement.first == config::constants::NODE_TAG) {
                        addToTmpFile(osmElement.second, cnst::NODE_TAG);
                    } else if (osmElement.first == config::constants::WAY_TAG) {
                        storeIdsOfReferencedNodes(osmElement.second);
                        addToTmpFile(osmElement.second, cnst::WAY_TAG);
                    } else if (osmElement.first == config::constants::RELATION_TAG) {
                        storeIdsOfReferencedElements(osmElement.second);
                        addToTmpFile(osmElement.second, cnst::RELATION_TAG);
                    }
                }
            } else if (changeset.first == config::constants::DELETE_TAG) {
                continue;
            }
        }
    }

    void OsmChangeHandler::getIdsForGeometryUpdate() {
        if (!_modifiedNodes.empty()) {
            doInBatches(
                _modifiedNodes,
            MAX_TRIPLE_COUNT_PER_QUERY,
            [this](const std::set<long long>& batch) {
                auto wayIds = _odf.fetchWaysReferencingNodes(batch);
                for (const auto &wayId: wayIds) {
                    if (!_modifiedWays.contains(wayId)) {
                        _waysToUpdateGeometry.insert(wayId);
                    }
                }
            });
        }

        if (!_modifiedNodes.empty()) {
            doInBatches(
                _modifiedNodes,
                MAX_TRIPLE_COUNT_PER_QUERY,
                [this](const std::set<long long>& batch) {
                    auto relationIds = _odf.fetchRelationsReferencingNodes(batch);
                    for (const auto &relId: relationIds) {
                        if (!_modifiedRelations.contains(relId)) {
                            _relationsToUpdateGeometry.insert(relId);
                        }
                    }
                });
        }

        std::set<long long> updatedWays;
        updatedWays.insert(_modifiedWays.begin(), _modifiedWays.end());
        updatedWays.insert(_waysToUpdateGeometry.begin(), _waysToUpdateGeometry.end());
        if (!updatedWays.empty()) {
            doInBatches(
            _modifiedWays,
            MAX_TRIPLE_COUNT_PER_QUERY,
            [this](const std::set<long long>& batch) {
                auto relationIds = _odf.fetchRelationsReferencingWays(batch);
                for (const auto &relId: relationIds) {
                    if (!_modifiedRelations.contains(relId)) {
                        _relationsToUpdateGeometry.insert(relId);
                    }
                }
            });
        }

        if (!_modifiedRelations.empty()) {
            doInBatches(
                _modifiedRelations,
            MAX_TRIPLE_COUNT_PER_QUERY,
            [this](const std::set<long long>& batch) {
                auto relationIds = _odf.fetchRelationsReferencingRelations(batch);
                for (const auto &relId: relationIds) {
                    if (!_modifiedRelations.contains(relId)) {
                        _relationsToUpdateGeometry.insert(relId);
                    }
                }
            });
        }
    }

    void OsmChangeHandler::getReferencedRelations() {
        if (!_relationsToUpdateGeometry.empty()) {
            doInBatches(
                    _relationsToUpdateGeometry,
                MAX_TRIPLE_COUNT_PER_QUERY,
                [this](const std::set<long long>& batch) {
                    auto relationIds = _odf.fetchRelationsReferencingRelations(batch);
                    for (const auto &relId: relationIds) {
                        if (!_relationsToUpdateGeometry.contains(relId) &&
                            !_createdRelations.contains(relId) &&
                            !_modifiedRelations.contains(relId)) {
                            _referencedRelations.insert(relId);
                        }
                    }
                });
        }
    }

    void OsmChangeHandler::getReferencedWays() {
        std::set<long long> relationsToFetchWaysFor;
        relationsToFetchWaysFor.insert(_referencedRelations.begin(), _referencedRelations.end());
        relationsToFetchWaysFor.insert(_relationsToUpdateGeometry.begin(), _relationsToUpdateGeometry.end());
        if (!relationsToFetchWaysFor.empty()) {
            doInBatches(
                    relationsToFetchWaysFor,
                    MAX_TRIPLE_COUNT_PER_QUERY,
                    [this](const std::set<long long>& batch) {
                    auto wayIds = _odf.fetchRelationMembersWay(batch);
                    for (const auto &wayId: wayIds) {
                        if (!_waysToUpdateGeometry.contains(wayId) &&
                            !_createdWays.contains(wayId) &&
                            !_modifiedWays.contains(wayId)) {
                            _referencedWays.insert(wayId);
                        }
                    }
                });
        }
    }

    void OsmChangeHandler::getReferencedNodes() {
        std::set<long long> relationsToFetchNodesFor;
        relationsToFetchNodesFor.insert(_referencedRelations.begin(),
                                        _referencedRelations.end());
        relationsToFetchNodesFor.insert(_relationsToUpdateGeometry.begin(),
                                        _relationsToUpdateGeometry.end());
        if (!relationsToFetchNodesFor.empty()) {
            doInBatches(
                relationsToFetchNodesFor,
                MAX_TRIPLE_COUNT_PER_QUERY,
                [this](const std::set<long long>& batch) {
                    auto nodeIds = _odf.fetchRelationMembersNode(batch);
                    for (const auto &nodeId: nodeIds) {
                        if (!nodeInChangeFile(nodeId)) {
                            _referencedNodes.insert(nodeId);
                        }
                    }
                });
        }

        std::set<long long> waysToFetchNodesFor;
        waysToFetchNodesFor.insert(_referencedWays.begin(), _referencedWays.end());
        waysToFetchNodesFor.insert(_waysToUpdateGeometry.begin(), _waysToUpdateGeometry.end());
        if (!waysToFetchNodesFor.empty()) {
            doInBatches(
            waysToFetchNodesFor,
            MAX_TRIPLE_COUNT_PER_QUERY,
            [this](const std::set<long long>& batch) {
                auto nodeIds = _odf.fetchWaysMembers(batch);
                for (const auto &nodeId: nodeIds) {
                    if (!nodeInChangeFile(nodeId)) {
                        _referencedNodes.insert(nodeId);
                    }
                }
            });
        }
    }

    void OsmChangeHandler::createDummyNodes() {
        std::vector<std::string> pointsAsWkt;
        std::vector<long long> nodeIds(_referencedNodes.begin(), _referencedNodes.end());

        doInBatches(
            _referencedNodes,
            MAX_TRIPLE_COUNT_PER_QUERY,
            [this, &pointsAsWkt](const std::set<long long>& batch) {
                auto pointsBatchResult = _odf.fetchNodeLocationsAsWkt(batch);
                pointsAsWkt.insert( pointsAsWkt.end(), pointsBatchResult.begin(), pointsBatchResult.end() );
            });

        for (auto it = nodeIds.begin(); it != nodeIds.end(); ++it) {
            long index = std::distance(nodeIds.begin(), it);
            auto dummyNode = olu::osm::OsmObjectHelper::createNodeFromPoint(
                    nodeIds.at(index),
                    pointsAsWkt.at(index));
            addToTmpFile(dummyNode, cnst::NODE_TAG);
        }
    }

    void OsmChangeHandler::createDummyWays() {
        for (const auto wayId: _referencedWays) {
            auto wayMembers = _odf.fetchWayMembers(wayId);
            auto dummyWay = olu::osm::OsmObjectHelper::createWayFromReferences(wayId, wayMembers);
            addToTmpFile(dummyWay, cnst::WAY_TAG);
        }
    }

    void OsmChangeHandler::createDummyRelations() {
        for (const auto relId: _referencedRelations) {
            auto members = _odf.fetchRelationMembers(relId);
            auto dummyRelation = olu::osm::OsmObjectHelper::createRelationFromReferences(relId, members);
            addToTmpFile(dummyRelation, cnst::RELATION_TAG);
        }
    }

    void OsmChangeHandler::sortFile(const std::string& elementTag) {
        std::string filename;
        if (elementTag == cnst::NODE_TAG) {
            filename = cnst::PATH_TO_NODE_FILE;
        } else if (elementTag == cnst::WAY_TAG) {
            filename = cnst::PATH_TO_WAY_FILE;
        } else if (elementTag == cnst::RELATION_TAG) {
            filename = cnst::PATH_TO_RELATION_FILE;
        }

        std::ifstream inputFile(filename);
        if (!inputFile.is_open()) {
            std::cerr << "Error opening file!" << std::endl;
            return;
        }

        std::vector<std::pair<std::string, int>> linesWithNumbers;
        std::string line;
        while (std::getline(inputFile, line)) {
            std::regex idRegex("id=\"(\\d+)");
            std::smatch match;
            long long id = -1;
            if (std::regex_search(line, match, idRegex)) {
                id = std::stoll(match[1]);
            } else {
                std::cout << "ID not found." << std::endl;
            }

            // Add the line and its extracted number to the vector
            linesWithNumbers.emplace_back(line, id);
        }
        inputFile.close();

        // Step 2: Sort the vector by the extracted numbers
        std::sort(linesWithNumbers.begin(), linesWithNumbers.end(),
                  [](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
                      return a.second < b.second; // Sort in ascending order
                  });

        // Step 3: Write the sorted lines back to the file
        std::ofstream outputFile(filename);
        if (!outputFile.is_open()) {
            std::cerr << "Error opening file!" << std::endl;
            return;
        }

        for (const auto& [line, number] : linesWithNumbers) {
            outputFile << line << '\n';
        }
        outputFile.close();
    }

    void OsmChangeHandler::deleteElementsFromDatabase() {
        std::set<long long> nodesToDelete;
        nodesToDelete.insert(_deletedNodes.begin(), _deletedNodes.end());
        nodesToDelete.insert(_modifiedNodes.begin(), _modifiedNodes.end());
        doInBatches(nodesToDelete,
            MAX_TRIPLE_COUNT_PER_QUERY/DELETE_TRIPLES_PER_NODE,
            [this](const std::set<long long>& batch) {
                auto query = sparql::QueryWriter::writeNodesDeleteQuery(batch);
                _sparql.setQuery(query);
                _sparql.setPrefixes(config::constants::PREFIXES_FOR_NODE_DELETE_QUERY);
//                _sparql.runQuery();
            });

        std::set<long long> waysToDelete;
        waysToDelete.insert(_deletedWays.begin(), _deletedWays.end());
        waysToDelete.insert(_modifiedWays.begin(), _modifiedWays.end());
        doInBatches(
                waysToDelete,
            MAX_TRIPLE_COUNT_PER_QUERY/DELETE_TRIPLES_PER_WAY,
            [this](const std::set<long long>& batch) {
                auto query = sparql::QueryWriter::writeWaysDeleteQuery(batch);
                _sparql.setQuery(query);
                _sparql.setPrefixes(config::constants::PREFIXES_FOR_WAY_DELETE_QUERY);
//                _sparql.runQuery();
            });

        std::set<long long> relationsToDelete;
        relationsToDelete.insert(_deletedRelations.begin(), _deletedRelations.end());
        relationsToDelete.insert(_modifiedRelations.begin(), _modifiedRelations.end());
        doInBatches(
                relationsToDelete,
            MAX_TRIPLE_COUNT_PER_QUERY/DELETE_TRIPLES_PER_RELATION,
            [this](const std::set<long long>& batch) {
                auto query = sparql::QueryWriter::writeRelationsDeleteQuery(batch);
                _sparql.setQuery(query);
                _sparql.setPrefixes(config::constants::PREFIXES_FOR_RELATION_DELETE_QUERY);
//                _sparql.runQuery();
            });
    }

    void OsmChangeHandler::insertElementsToDatabase() {
        filterRelevantTriples();
        createAndRunInsertQuery();
    }

    std::tuple<std::string, std::string, std::string>
    getElementsFromTriple(const std::string& triple) {
        std::regex regex(R"((\S+)\s+(\S+)\s+(\S+))");
        std::smatch match;
        if (std::regex_search(triple, match, regex)) {
            return std::make_tuple(match[1], match[2], match[3]);
        }

        std::string msg = "Cant split triple: " + triple;
        throw OsmChangeHandlerException(msg.c_str());
    }

    long long getIdFromTriple(const std::string& triple, const std::string& elementTag) {
        if (elementTag == cnst::NODE_TAG) {
            std::regex integerRegex(R"((?:osmnode:|osm_node_)(\d+))");
            std::smatch match;
            if (std::regex_search(triple, match, integerRegex)) {
                return stoll(match[1]);
            }

            std::string msg = "Cant get id for element: " + elementTag + " from triple: " + triple;
            throw OsmChangeHandlerException(msg.c_str());
        }

        if (elementTag == cnst::WAY_TAG) {
            std::regex integerRegex(R"((?:osmway:|osm_wayarea_)(\d+))");
            std::smatch match;
            if (std::regex_search(triple, match, integerRegex)) {
                return stoll(match[1]);
            }

            std::string msg = "Cant get id for element: " + elementTag + " from triple: " + triple;
            throw OsmChangeHandlerException(msg.c_str());
        }

        if (elementTag == cnst::RELATION_TAG) {
            std::regex integerRegex(R"((?:osmrel:|osm_relarea_)(\d+))");
            std::smatch match;
            if (std::regex_search(triple, match, integerRegex)) {
                return stoll(match[1]);
            }

            std::string msg = "Cant get id for element: " + elementTag + " from triple: " + triple;
            throw OsmChangeHandlerException(msg.c_str());
        }

        std::string msg = "Wrong element tag: " + elementTag;
        throw OsmChangeHandlerException(msg.c_str());
    }

    void OsmChangeHandler::filterRelevantTriples() {
        std::string dataDecompressed = olu::util::Decompressor::readBzip2(cnst::PATH_TO_OUTPUT_FILE);
        std::istringstream iss(dataDecompressed);

        std::set<long long> nodesToInsert;
        nodesToInsert.insert(_createdNodes.begin(), _createdNodes.end());
        nodesToInsert.insert(_modifiedNodes.begin(), _modifiedNodes.end());

        std::set<long long> waysToInsert;
        waysToInsert.insert(_createdWays.begin(), _createdWays.end());
        waysToInsert.insert(_modifiedWays.begin(), _modifiedWays.end());
        waysToInsert.insert(_waysToUpdateGeometry.begin(), _waysToUpdateGeometry.end());

        std::set<long long> relationsToInsert;
        relationsToInsert.insert(_createdRelations.begin(), _createdRelations.end());
        relationsToInsert.insert(_modifiedRelations.begin(), _modifiedRelations.end());
        relationsToInsert.insert(_relationsToUpdateGeometry.begin(), _relationsToUpdateGeometry.end());

        std::ofstream triplesFile;
        triplesFile.open (cnst::PATH_TO_TRIPLES_FILE, std::ios::app);

        std::set<std::string> relevantMemberIds;
        std::string line;
        while (std::getline(iss, line)) {
            if (line.starts_with("@")) {
                continue;
            }

            auto [subject, predicate, object] = getElementsFromTriple(line);
            if (subject.starts_with("osmnode:") || subject.starts_with("osm2rdfgeom:osm_node_")) {
                auto nodeId = getIdFromTriple(line, cnst::NODE_TAG);
                if (nodesToInsert.contains(nodeId)) {
                    triplesFile << line << std::endl;
                }

                continue;
            }

            if (subject.starts_with("osmway:") || subject.starts_with("osm2rdfgeom:osm_wayarea_")) {
                auto wayId = getIdFromTriple(line, cnst::WAY_TAG);
                if (waysToInsert.contains(wayId)) {
                    triplesFile << line << std::endl;

                    if (predicate == "osmway:node") {
                        relevantMemberIds.insert(object);
                    }
                }

                continue;
            }

            if (subject.starts_with("osmrel:") || subject.starts_with("osm2rdfgeom:osm_relarea_")) {
                auto relId = getIdFromTriple(line, cnst::RELATION_TAG);
                if (relationsToInsert.contains(relId)) {
                    triplesFile << line << std::endl;

                    if (predicate == "osmrel:member") {
                        relevantMemberIds.insert(object);
                    }
                }

                continue;
            }

            if (relevantMemberIds.contains(subject)) {
                triplesFile << line << std::endl;
                continue;
            }
        }

        triplesFile.close();
    }

    size_t OsmChangeHandler::countElements(const boost::property_tree::ptree &osmChangeElement) {
        size_t count = 0;
        // Loop over all change elements in the change file ('modify', 'delete' or 'create')
        for (const auto &child : osmChangeElement.get_child(
                config::constants::OSM_CHANGE_TAG)) {
            if (child.first == config::constants::MODIFY_TAG ||
                child.first == config::constants::CREATE_TAG ||
                child.first == config::constants::DELETE_TAG) {
                count += child.second.size();
            }
        }
        return count;
    }

    void
    OsmChangeHandler::storeIdsOfReferencedNodes(const boost::property_tree::ptree& wayElement) {
        for (const auto &child : wayElement.get_child("")) {
            if (child.first != config::constants::NODE_REFERENCE_TAG) {
                continue;
            }

            std::string idAsString;
            try {
                idAsString = util::XmlReader::readAttribute(
                        config::constants::NODE_REFERENCE_ATTRIBUTE,
                        child.second);
            } catch (std::exception &e) {
                std::cerr << e.what() << std::endl;
                std::string msg = "Exception while trying to read id of node reference: "
                                  + util::XmlReader::readTree(child.second);
                throw OsmChangeHandlerException(msg.c_str());
            }

            long long id;
            try {
                id = std::stol(idAsString);
            } catch(std::exception &e) {
                std::cerr << e.what() << std::endl;
                std::string msg = "Exception while trying to convert id string: "
                                  + idAsString + " to long";
                throw OsmChangeHandlerException(msg.c_str());
            }

            if (!nodeInChangeFile(id)) {
                _referencedNodes.insert(id);
            }
        }
    }

    void
    OsmChangeHandler::storeIdsOfReferencedElements(const boost::property_tree::ptree& relElement) {
        for (const auto &member : relElement.get_child("")) {
            if (member.first != "member") {
                continue;
            }

            auto type = util::XmlReader::readAttribute("<xmlattr>.type",
                                                       member.second);
            auto refIdAsString = util::XmlReader::readAttribute("<xmlattr>.ref",
                                                                member.second);
            long long id;
            try {
                id = std::stoll(refIdAsString);
            } catch(std::exception &e) {
                std::cerr << e.what() << std::endl;
                std::string msg = "Exception while trying to convert id string: "
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
                std::string msg = "Cant handle member type: " + type + " for relation: "
                        + util::XmlReader::readTree(relElement);
                throw OsmChangeHandlerException(msg.c_str());
            }
        }

    }

    void OsmChangeHandler::createAndRunInsertQuery() {
        std::ifstream file(cnst::PATH_TO_TRIPLES_FILE);
        std::vector<std::string> triples;
        std::string line;
        // Read each line from the file and add it to the vector
        while (std::getline(file, line)) {
            triples.push_back(line);
        }

        // Close the file
        file.close();

        // QLever has a maximum number of triples it can handle in one query, so we have to
        // divide the triples in batches
        std::vector<std::vector<std::string>> triplesBatches;
        for (auto it = triples.cbegin(), e = triples.cend(); it != triples.cend(); it = e) {
            e = it + std::min<std::size_t>(triples.cend() - it, MAX_TRIPLE_COUNT_PER_QUERY);
            triplesBatches.emplace_back(it, e);
        }

        // Create a sparql query for each batch and send it to the sparql endpoint.
        for (auto & batch : triplesBatches) {
            auto query = sparql::QueryWriter::writeInsertQuery(batch);
            _sparql.setPrefixes(cnst::DEFAULT_PREFIXES);
            _sparql.setQuery(query);

            try {
//                _sparql.runQuery();
            } catch (std::exception &e) {
                std::cerr << e.what() << std::endl;
                throw OsmChangeHandlerException(
                        "Exception while trying to run sparql query for insertion");
            }
        }
    }
} // namespace olu::osm
