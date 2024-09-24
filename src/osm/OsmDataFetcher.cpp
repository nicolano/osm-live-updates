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

#include <string>
#include <vector>
#include <boost/regex.hpp>
#include <boost/property_tree/ptree.hpp>
#include <iostream>
#include <fstream>

namespace constants = olu::config::constants;

namespace olu::osm {

    // _____________________________________________________________________________________________
    OsmDatabaseState OsmDataFetcher::fetchDatabaseState(int sequenceNumber) const {
        // Build url for state file
        std::string seqNumberFormatted =
                util::URLHelper::formatSequenceNumberForUrl(sequenceNumber);
        std::string stateFileName =
                seqNumberFormatted + "." + constants::OSM_DIFF_STATE_FILE + constants::TXT_EXTENSION;

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
        pathSegments.emplace_back(constants::OSM_DIFF_STATE_FILE + constants::TXT_EXTENSION);
        std::string url = util::URLHelper::buildUrl(pathSegments);

        // Get state file from osm server
        auto request = util::HttpRequest(util::GET, url);
        std::string response = request.perform();
        return extractStateFromStateFile(response);
    }

    // _____________________________________________________________________________________________
    std::string OsmDataFetcher::fetchChangeFile(int &sequenceNumber) {
        // Build url for change file
        std::string sequenceNumberFormatted = util::URLHelper::formatSequenceNumberForUrl(sequenceNumber);
        std::string diffFilename = sequenceNumberFormatted + constants::OSM_CHANGE_FILE_EXTENSION + constants::GZIP_EXTENSION;
        std::vector<std::string> pathSegments;
        pathSegments.emplace_back(_config.osmChangeFileDirectoryUri);
        pathSegments.emplace_back(diffFilename);
        std::string url = util::URLHelper::buildUrl(pathSegments);

        // Get change file from server and write to cache file.
        std::string filePath = constants::DIFF_CACHE_FILE + std::to_string(sequenceNumber) + constants::OSM_CHANGE_FILE_EXTENSION + constants::GZIP_EXTENSION;
        auto request = util::HttpRequest(util::GET, url);

        auto response = request.perform();
        std::ofstream outputFile;
        outputFile.open (filePath);
        outputFile << response;
        outputFile.close();

        return filePath;
    }

    // _____________________________________________________________________________________________
    std::string OsmDataFetcher::fetchNodeLocationAsWkt(const int &nodeId) {
        auto query = olu::sparql::QueryWriter::writeQueryForNodeLocation(nodeId);
        _sparqlWrapper.setMethod(util::HttpMethod::GET);
        _sparqlWrapper.setQuery(query);
        _sparqlWrapper.setPrefixes(constants::PREFIXES_FOR_NODE_LOCATION);
        auto response = _sparqlWrapper.runQuery();
        boost::property_tree::ptree responseAsTree;
        olu::util::XmlReader::populatePTreeFromString(response, responseAsTree);

        std::string pointAsWkt;
        try {
            pointAsWkt = responseAsTree.get<std::string>(
                    constants::PATH_TO_SPARQL_RESULT);
        } catch (boost::property_tree::ptree_bad_path &e) {
            std::cerr << "Could not get location for node with id "
                          + std::to_string(nodeId)
                          + " from sparql endpoint" << std::endl;
            throw OsmDataFetcherException(
                    "Exception while trying to get location for node");
        }

        return pointAsWkt;
    }

    // _____________________________________________________________________________________________
    std::string OsmDataFetcher::fetchLatestTimestampOfAnyNode() {
        auto query = olu::sparql::QueryWriter::writeQueryForLatestNodeTimestamp();
        _sparqlWrapper.setMethod(util::HttpMethod::GET);
        _sparqlWrapper.setQuery(query);
        _sparqlWrapper.setPrefixes(constants::PREFIXES_FOR_LATEST_NODE_TIMESTAMP);
        auto response = _sparqlWrapper.runQuery();

        boost::property_tree::ptree responseAsTree;
        olu::util::XmlReader::populatePTreeFromString(response, responseAsTree);

        std::string timestamp;
        try {
            timestamp = responseAsTree.get<std::string>(
                    constants::PATH_TO_SPARQL_RESULT);
        } catch (boost::property_tree::ptree_bad_path &e) {
            std::cerr
            << "Could not fetch latest timestamp of any node from sparql endpoint"
            << std::endl;
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
        boost::smatch matchSeqNumber;
        if (boost::regex_search(stateFile, matchSeqNumber, regexSeqNumber)) {
            std::string number = matchSeqNumber[1];
            ods.sequenceNumber = std::stoi(number);
        } else {
            throw OsmDataFetcherException(
                    "Sequence number of latest database state could not be fetched");
        }

        // Extract timestamp from state file
        boost::regex regexTimestamp(
                R"(timestamp=([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}\\:[0-9]{2}\\:[0-9]{2}Z))");
        boost::smatch matchTimestamp;
        if (boost::regex_search(stateFile, matchTimestamp, regexTimestamp)) {
            std::string timestamp = matchTimestamp[1];
            ods.timeStamp = timestamp;
        } else {
            throw OsmDataFetcherException(
                    "Timestamp of latest database state could not be fetched");
        }

        return ods;
    }

} // namespace olu



