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

#include "osm/OsmDataFetcher.h"
#include "config/Constants.h"
#include "util/URLHelper.h"
#include "util/HttpRequest.h"
#include "util/XmlReader.h"
#include "sparql/QueryWriter.h"

#include <vector>
#include <boost/regex.hpp>
#include <boost/property_tree/ptree.hpp>
#include <iostream>
#include <fstream>

namespace cnst = olu::config::constants;

namespace olu::osm {
    boost::property_tree::ptree OsmDataFetcher::runQuery(const std::string &query,
                                                         const std::vector<std::string> &prefixes) {
        _sparqlWrapper.setQuery(query);
        _sparqlWrapper.setPrefixes(prefixes);
        return _sparqlWrapper.runQuery();
    }

    // _____________________________________________________________________________________________
    OsmDatabaseState OsmDataFetcher::fetchDatabaseState(int sequenceNumber) const {
        // Build url for state file
        std::string seqNumberFormatted =
                util::URLHelper::formatSequenceNumberForUrl(sequenceNumber);
        std::string stateFileName =
                seqNumberFormatted + ".state.txt";

        std::vector<std::string> pathSegments { };
        pathSegments.emplace_back(_config.osmChangeFileDirectoryUri);
        pathSegments.emplace_back(stateFileName);
        std::string url = util::URLHelper::buildUrl(pathSegments);

        //  state file from osm server
        auto request = util::HttpRequest(util::GET, url);

        std::string response;
        response = request.perform();

        return extractStateFromStateFile(response);
    }

    // _____________________________________________________________________________________________
    OsmDatabaseState OsmDataFetcher::fetchLatestDatabaseState() const {
        // Build url for state file
        std::vector<std::string> pathSegments { };
        pathSegments.emplace_back(_config.osmChangeFileDirectoryUri);
        pathSegments.emplace_back("state.txt");
        const std::string url = util::URLHelper::buildUrl(pathSegments);

        // Get state file from osm server
        auto request = util::HttpRequest(util::GET, url);
        const std::string response = request.perform();
        return extractStateFromStateFile(response);
    }

    // _____________________________________________________________________________________________
    std::string OsmDataFetcher::fetchChangeFile(int &sequenceNumber) {
        // Build url for change file
        std::string sequenceNumberFormatted = util::URLHelper::formatSequenceNumberForUrl(
            sequenceNumber);
        std::string diffFilename = sequenceNumberFormatted + cnst::OSM_CHANGE_FILE_EXTENSION +
                                   cnst::GZIP_EXTENSION;
        std::vector<std::string> pathSegments;
        pathSegments.emplace_back(_config.osmChangeFileDirectoryUri);
        pathSegments.emplace_back(diffFilename);
        std::string url = util::URLHelper::buildUrl(pathSegments);

        // Get change file from server and write to cache file.
        std::string filePath = cnst::DIFF_CACHE_FILE + std::to_string(sequenceNumber) +
                               cnst::OSM_CHANGE_FILE_EXTENSION + cnst::GZIP_EXTENSION;
        auto request = util::HttpRequest(util::GET, url);

        auto response = request.perform();
        std::ofstream outputFile;
        outputFile.open(filePath);
        outputFile << response;
        outputFile.close();

        return filePath;
    }

    std::vector<Node>
    OsmDataFetcher::fetchNodes(const std::set<id_t> &nodeIds) {
        auto response = runQuery(sparql::QueryWriter::writeQueryForNodeLocations(nodeIds),
                                       cnst::PREFIXES_FOR_NODE_LOCATION);

        std::vector<Node> nodes;
        for (const auto &result : response.get_child("sparql.results")) {
            id_t id;
            std::string locationAsWkt;
            for (const auto &binding : result.second.get_child("")) {
                auto name = util::XmlReader::readAttribute("<xmlattr>.name", binding.second);
                if (name == "nodeGeo") {
                    auto uri = binding.second.get<std::string>("uri");

                    try {
                        id = std::stoll(uri.substr(cnst::OSM_GEOM_NODE_URI.length()));
                    } catch (const std::exception &e) {
                        std::cerr << e.what() << std::endl;
                        std::string msg = "Exception while trying to get id from uri: " + uri;
                        throw OsmDataFetcherException(msg.c_str());
                    }
                }

                if (name == "location") {
                    locationAsWkt = binding.second.get<std::string>("literal");
                }
            }

            nodes.emplace_back( id, locationAsWkt );
        }

        if (nodes.size() > nodeIds.size()) {
            std::cout
                << "The SPARQL endpoint returned " << std::to_string(nodes.size())
                << " locations, for " << std::to_string(nodeIds.size())
                << " nodes." << std::endl;
            throw OsmDataFetcherException("Exception while trying to fetch nodes locations");
        }

        return nodes;
    }

    // _____________________________________________________________________________________________
    std::string OsmDataFetcher::fetchLatestTimestampOfAnyNode() {
        const auto response = runQuery(sparql::QueryWriter::writeQueryForLatestNodeTimestamp(),
                                        cnst::PREFIXES_FOR_LATEST_NODE_TIMESTAMP);

        std::string timestamp;
        try {
            timestamp = response.get<std::string>("sparql.results.result.binding.literal");
        } catch (std::exception &e) {
            std::cerr << e.what() << std::endl;
            throw OsmDataFetcherException(
                    "Could not fetch latest timestamp of any node from sparql endpoint");
        }

        return timestamp;
    }

    // _____________________________________________________________________________________________
    OsmDatabaseState
    OsmDataFetcher::fetchDatabaseStateForTimestamp(const std::string& timeStamp) const {
        OsmDatabaseState state = fetchLatestDatabaseState();
        while (true) {
            if (state.timeStamp > timeStamp) {
                auto seq = state.sequenceNumber;
                seq--;
                state = fetchDatabaseState(seq);
            } else {
                return state;
            }
        }
    }

    // _____________________________________________________________________________________________
    OsmDatabaseState OsmDataFetcher::extractStateFromStateFile(const std::string& stateFile) {
        OsmDatabaseState ods;
        // Extract sequence number from state file
        boost::regex regexSeqNumber("sequenceNumber=(\\d+)");
        if (boost::smatch matchSeqNumber; regex_search(stateFile, matchSeqNumber, regexSeqNumber)) {
            std::string number = matchSeqNumber[1];
            ods.sequenceNumber = std::stoi(number);
        } else {
            throw OsmDataFetcherException(
                    "Sequence number of latest database state could not be fetched");
        }

        // Extract timestamp from state file
        boost::regex regexTimestamp(
                R"(timestamp=([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}\\:[0-9]{2}\\:[0-9]{2}Z))");
        if (boost::smatch matchTimestamp; regex_search(stateFile, matchTimestamp, regexTimestamp)) {
            std::string timestamp = matchTimestamp[1];
            ods.timeStamp = timestamp;
        } else {
            throw OsmDataFetcherException(
                    "Timestamp of latest database state could not be fetched");
        }

        return ods;
    }

    // _________________________________________________________________________________________________
    std::vector<Relation>
    OsmDataFetcher::fetchRelations(const std::set<id_t> &relationIds) {
        auto response = runQuery(sparql::QueryWriter::writeQueryForRelationMembers(relationIds),
                                    cnst::PREFIXES_FOR_RELATION_MEMBERS);

        Relation currentRelation(0);
        std::vector<Relation> relations;
        for (const auto &result : response.get_child("sparql.results")) {
            std::string memberUri;
            std::string role;
            id_t relationId;
            std::string relationType;
            for (const auto &binding : result.second.get_child("")) {
                auto name = util::XmlReader::readAttribute("<xmlattr>.name",binding.second);
                if (name == "rel") {
                    auto relUri = binding.second.get<std::string>("uri");
                    relationId = std::stoll(relUri.substr(cnst::OSM_REL_URI.length()));
                }

                if (name == "key") {
                    relationType = binding.second.get<std::string>("literal");
                }

                if (name == "id") {
                    memberUri = binding.second.get<std::string>("uri");
                }

                if (name == "role") {
                    role = binding.second.get<std::string>("literal");
                }
            }

            if (currentRelation.getId() != relationId) {
                relations.push_back(currentRelation);
                currentRelation = Relation(relationId);
                currentRelation.setType(relationType);
            }

            if (memberUri.starts_with(cnst::OSM_NODE_URI)) {
                id_t nodeId = std::stoll( memberUri.substr(cnst::OSM_WAY_URI.length()));
                currentRelation.addNodeAsMember(nodeId, role);
            } else if (memberUri.starts_with(cnst::OSM_WAY_URI)) {
                id_t wayId = std::stoll( memberUri.substr(cnst::OSM_WAY_URI.length()));
                currentRelation.addNodeAsMember(wayId, role);
            } else if (memberUri.starts_with(cnst::OSM_REL_URI)) {
                id_t relId = std::stoll( memberUri.substr(cnst::OSM_WAY_URI.length()));
                currentRelation.addNodeAsMember(relId, role);
            }
        }

        // Delete first placeholder way from list
        if (!relations.empty()) {
            relations.erase(relations.begin());
        }
        return relations;
    }

    std::vector<Way> OsmDataFetcher::fetchWays(const std::set<id_t> &wayIds) {
        auto response = runQuery(sparql::QueryWriter::writeQueryForWaysMembers(wayIds),
                                    cnst::PREFIXES_FOR_WAY_MEMBERS);

        std::map<id_t, std::vector<id_t>> wayMap;
        for (const auto &result : response.get_child("sparql.results")) {
            id_t wayId;
            for (const auto &binding : result.second.get_child("")) {
                auto name = util::XmlReader::readAttribute("<xmlattr>.name", binding.second);
                if (name == "way") {
                    auto uri = binding.second.get<std::string>("uri");
                    wayId = std::stoll( uri.substr(cnst::OSM_WAY_URI.length()) );

                    if (!wayMap.contains(wayId)) {
                        wayMap[wayId] = std::vector<id_t>();
                    }
                }

                if (name == "node") {
                    auto uri = binding.second.get<std::string>("uri");
                    id_t nodeId = std::stoll( uri.substr(cnst::OSM_NODE_URI.length()) );
                    wayMap[wayId].push_back(nodeId);
                }
            }
        }

        std::vector<Way> ways;
        for (const auto &[wayId, nodeIds] : wayMap) {
            Way way(wayId);
            for (const auto &nodeId : nodeIds) {
                way.addMember(nodeId);
            }
            ways.push_back(way);
        }

        return ways;
    }

    std::vector<id_t> OsmDataFetcher::fetchWaysMembers(const std::set<id_t> &wayIds) {
        auto response = runQuery(sparql::QueryWriter::writeQueryForReferencedNodes(wayIds),
                                    cnst::PREFIXES_FOR_WAY_MEMBERS);

        std::vector<id_t> nodeIds;
        for (const auto &result : response.get_child("sparql.results")) {
            auto memberSubject = result.second.get<std::string>("binding.uri");

            // The endpoint will return the uri of the way, so we have to extract the id from it
            std::string nodeIdAsString = memberSubject.substr(cnst::OSM_NODE_URI.length());
            id_t nodeId;
            try {
                nodeId = std::stoll(nodeIdAsString);

                if (nodeId <= 0) {
                    const std::string msg = "Invalid id: " + nodeIdAsString;
                    throw OsmDataFetcherException(msg.c_str());
                }
            } catch (std::exception &e) {
                std::cerr << e.what() << std::endl;
                std::string msg = "Could extract way id from uri: " + memberSubject;
                throw OsmDataFetcherException(msg.c_str());
            }

            nodeIds.emplace_back(nodeId);
        }

        return nodeIds;
    }

    std::pair<std::vector<id_t>, std::vector<id_t>>
    OsmDataFetcher::fetchRelationMembers(const std::set<id_t> &relIds) {
        auto response = runQuery(sparql::QueryWriter::writeQueryForRelationMembers(relIds),
                                    cnst::PREFIXES_FOR_RELATION_MEMBERS);

        std::vector<id_t> nodeIds;
        std::vector<id_t> wayIds;
        for (const auto &result : response.get_child("sparql.results")) {
            auto memberSubject = result.second.get<std::string>("binding.uri");

            id_t id;
            if (memberSubject.starts_with(cnst::OSM_NODE_URI)) {
                std::string nodeIdAsString = memberSubject.substr(cnst::OSM_NODE_URI.length());
                try {
                    id = std::stoll(nodeIdAsString);
                    nodeIds.emplace_back(id);

                    if (id <= 0) {
                        std::string msg = "Invalid id: " + nodeIdAsString;
                        throw OsmDataFetcherException(msg.c_str());
                    }
                } catch (std::exception &e) {
                    std::cerr << e.what() << std::endl;
                    std::string msg = "Could extract way id from uri: " + memberSubject;
                    throw OsmDataFetcherException(msg.c_str());
                }
            } else if (memberSubject.starts_with(cnst::OSM_WAY_URI)) {
                std::string wayIdAsString = memberSubject.substr(cnst::OSM_WAY_URI.length());
                try {
                    id = std::stoll(wayIdAsString);
                    wayIds.emplace_back(id);

                    if (id <= 0) {
                        std::string msg = "Invalid id: " + wayIdAsString;
                        throw OsmDataFetcherException(msg.c_str());
                    }
                } catch (std::exception &e) {
                    std::cerr << e.what() << std::endl;
                    std::string msg = "Could extract way id from uri: " + memberSubject;
                    throw OsmDataFetcherException(msg.c_str());
                }
            }
        }

        return { nodeIds, wayIds };
    }

    // _________________________________________________________________________________________________
    std::vector<id_t> OsmDataFetcher::fetchWaysReferencingNodes(const std::set<id_t> &nodeIds) {
        auto response = runQuery(sparql::QueryWriter::writeQueryForWaysReferencingNodes(nodeIds),
                                    cnst::PREFIXES_FOR_WAYS_REFERENCING_NODE);

        std::vector<id_t> memberSubjects;
        for (const auto &result : response.get_child("sparql.results")) {
            auto memberSubject = result.second.get<std::string>("binding.uri");

            // The endpoint will return the uri of the way, so we have to extract the id from it
            std::string wayIdAsString = memberSubject.substr(cnst::OSM_WAY_URI.length());
            id_t wayId;
            try {
                wayId = std::stoll(wayIdAsString);

                if (wayId <= 0) {
                    const std::string msg = "Invalid id: " + wayIdAsString;
                    throw OsmDataFetcherException(msg.c_str());
                }
            } catch (std::exception &e) {
                std::cerr << e.what() << std::endl;
                const std::string msg = "Could extract way id from uri: " + memberSubject;
                throw OsmDataFetcherException(msg.c_str());
            }

            memberSubjects.emplace_back(wayId);
        }

        return memberSubjects;
    }

    // _____________________________________________________________________________________________
    std::vector<id_t>
    OsmDataFetcher::fetchRelationsReferencingNodes(const std::set<id_t> &nodeIds) {
        auto response = runQuery(sparql::QueryWriter::writeQueryForRelationsReferencingNodes(nodeIds),
                                    cnst::PREFIXES_FOR_RELATIONS_REFERENCING_NODE);

        std::vector<id_t> memberSubjects;
        for (const auto &result : response.get_child(
                "sparql.results")) {
            auto memberSubject = result.second.get<std::string>("binding.uri");

            // The endpoint will return the uri of the way, so we have to extract the id from it
            std::string wayIdAsString = memberSubject.substr(cnst::OSM_REL_URI.length());
            id_t wayId;
            try {
                wayId = std::stoll(wayIdAsString);

                if (wayId <= 0) {
                    const std::string msg = "Invalid id: " + wayIdAsString;
                    throw OsmDataFetcherException(msg.c_str());
                }
            } catch (std::exception &e) {
                std::cerr << e.what() << std::endl;
                std::string msg = "Could extract way id from uri: " + memberSubject;
                throw OsmDataFetcherException(msg.c_str());
            }

            memberSubjects.emplace_back(wayId);
        }

        return memberSubjects;
    }

    // _________________________________________________________________________________________________
    std::vector<id_t> OsmDataFetcher::fetchRelationsReferencingWays(const std::set<id_t> &wayIds) {
        auto response = runQuery(sparql::QueryWriter::writeQueryForRelationsReferencingWays(wayIds),
                                    cnst::PREFIXES_FOR_RELATIONS_REFERENCING_WAY);

        std::vector<id_t> memberSubjects;
        for (const auto &result : response.get_child(
                "sparql.results")) {
            auto memberSubject = result.second.get<std::string>("binding.uri");

            // The endpoint will return the uri of the way, so we have to extract the id from it
            std::string wayIdAsString = memberSubject.substr(cnst::OSM_REL_URI.length());
            id_t wayId;
            try {
                wayId = std::stoll(wayIdAsString);

                if (wayId <= 0) {
                    const std::string msg = "Invalid id: " + wayIdAsString;
                    throw OsmDataFetcherException(msg.c_str());
                }
            } catch (std::exception &e) {
                std::cerr << e.what() << std::endl;
                std::string msg = "Could extract way id from uri: " + memberSubject;
                throw OsmDataFetcherException(msg.c_str());
            }

            memberSubjects.emplace_back(wayId);
        }

        return memberSubjects;
    }

    // _____________________________________________________________________________________________
    std::vector<id_t>
    OsmDataFetcher::fetchRelationsReferencingRelations(const std::set<id_t> &relationIds) {
        auto response = runQuery(
            sparql::QueryWriter::writeQueryForRelationsReferencingRelations(relationIds),
            cnst::PREFIXES_FOR_RELATIONS_REFERENCING_RELATIONS);

        std::vector<id_t> memberSubjects;
        for (const auto &result : response.get_child("sparql.results")) {
            auto memberSubject = result.second.get<std::string>("binding.uri");

            // The endpoint will return the uri of the way, so we have to extract the id from it
            std::string wayIdAsString = memberSubject.substr(cnst::OSM_REL_URI.length());
            id_t relId;
            try {
                relId = std::stoll(wayIdAsString);

                if (relId <= 0) {
                    const std::string msg = "Invalid id: " + wayIdAsString;
                    throw OsmDataFetcherException(msg.c_str());
                }
            } catch (std::exception &e) {
                std::cerr << e.what() << std::endl;
                std::string msg = "Could extract relation id from uri: " + memberSubject;
                throw OsmDataFetcherException(msg.c_str());
            }

            memberSubjects.emplace_back(relId);
        }

        return memberSubjects;
    }

} // namespace olu
