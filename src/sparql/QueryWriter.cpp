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

#include "sparql/QueryWriter.h"
#include "config/Constants.h"
#include "util/XmlReader.h"

#include <string>
#include <vector>
#include <iostream>

// _________________________________________________________________________________________________
std::string olu::sparql::QueryWriter::writeInsertQuery(const std::vector<std::string>& triples) {
    std::string query;
    query += "INSERT DATA { ";

    for (const auto & element : triples) {
        query += element + " ";
    }

    query += "}";
    return query;
}

// _________________________________________________________________________________________________
std::string olu::sparql::QueryWriter::writeDeleteQuery(const std::vector<std::string>& subjects) {
    std::string query;
    query += "DELETE WHERE { ";

    for (size_t i = 0; i < subjects.size(); ++i) {
        query += subjects[i] + " ?p" + std::to_string(i) + " ?o" + std::to_string(i) + " . ";
    }

    query += "}";
    return query;
}

// _________________________________________________________________________________________________
std::string olu::sparql::QueryWriter::writeNodeDeleteQuery(const long long &nodeId) {
    std::vector<std::string> triples {
            "osmnode:" + std::to_string(nodeId),
            "osm2rdfgeom:osm_node_" + std::to_string(nodeId)
    };

    return writeDeleteQuery(triples);
}

// _________________________________________________________________________________________________
std::string olu::sparql::QueryWriter::writeWayDeleteQuery(const long long &wayId) {
    std::vector<std::string> triples {
            "osmway:" + std::to_string(wayId),
            "osm2rdf:way_" + std::to_string(wayId),
            "osm2rdfgeom:osm_wayarea_" + std::to_string(wayId),
    };

    return writeDeleteQuery(triples);
}

// _________________________________________________________________________________________________
std::string olu::sparql::QueryWriter::writeRelationDeleteQuery(const long long &relationId) {
    std::vector<std::string> triples {
            "osmrel:" + std::to_string(relationId),
            "osm2rdfgeom:osm_relarea_" + std::to_string(relationId),
    };

    return writeDeleteQuery(triples);
}

// _________________________________________________________________________________________________
std::string olu::sparql::QueryWriter::writeQueryForNodeLocation(const long long &nodeId) {
    std::string query = "SELECT ?o WHERE { osm2rdfgeom:osm_node_" + std::to_string(nodeId) + " geo:asWKT ?o . }";
    return query;
}

// _________________________________________________________________________________________________
std::string
olu::sparql::QueryWriter::writeQueryForNodeLocations(const std::vector<long long> &nodeIds) {
    std::string query;
    query += "SELECT ?o WHERE { ";

    for (const auto & nodeId : nodeIds) {
        if (nodeId == nodeIds.front()) {
            query +=  + " { osm2rdfgeom:osm_node_" + std::to_string(nodeId) + " geo:asWKT ?o .  }";
        } else {
            query +=  + " UNION { osm2rdfgeom:osm_node_" + std::to_string(nodeId) + " geo:asWKT ?o .  }";
        }
    }

    query += "}";
    return query;
}

// _________________________________________________________________________________________________
std::string olu::sparql::QueryWriter::writeQueryForLatestNodeTimestamp() {
    std::string query = "SELECT ?p WHERE { ?s rdf:type osm:node . ?s osmmeta:timestamp ?p . } "
                        "ORDER BY DESC(?p) LIMIT 1";
    return query;
}

// _________________________________________________________________________________________________
std::string
olu::sparql::QueryWriter::writeQueryForRelationMembers(const long long &relationId) {
    std::string query = "SELECT ?o WHERE { "
                        "osmrel:" + std::to_string(relationId) + " osmrel:member ?o . "
                        "}";
    return query;
}
