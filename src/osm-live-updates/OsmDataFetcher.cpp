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

#include "osm-live-updates/OsmDataFetcher.h"
#include "osm-live-updates/config/Constants.h"
#include "osm-live-updates/util/URLHelper.h"
#include "osm-live-updates/util/HttpClient.h"

#include <string>
#include <vector>
#include <boost/regex.hpp>

namespace constants = olu::config::constants;

namespace olu {

// _________________________________________________________________________________________________
std::string OsmDataFetcher::fetchLatestSequenceNumber() {
    // Build url for state file
    std::vector<std::string> pathSegments;
    pathSegments.emplace_back(constants::OSM_REPLICATION_BASE_URL);
    pathSegments.emplace_back("minute");
    pathSegments.emplace_back(constants::OSM_DIFF_STATE_FILE + constants::TXT_EXTENSION);
    std::string url = util::URLHelper::buildUrl(pathSegments);

    // Get state file from osm server
    std::string readBuffer = util::HttpClient::perform_get(url);

    // Extract sequence number from state file
    boost::regex regex("sequenceNumber=(\\d+)");
    boost::smatch match;
    if (boost::regex_search(readBuffer, match, regex)) {
        std::string number = match[1];
        return number;
    } else {
       // TODO: Throw error if nothing found.
    }
}

// _________________________________________________________________________________________________
void OsmDataFetcher::fetchDiffWithSequenceNumber(int &sequenceNumber) {

}

} // namespace olu



