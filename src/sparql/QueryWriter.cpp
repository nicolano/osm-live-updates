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

#include <string>
#include <vector>
#include <sstream>

// _________________________________________________________________________________________________
std::string olu::sparql::QueryWriter::writeInsertQuery(const std::vector<std::string>& triples) {
    std::ostringstream ss;
    ss << "INSERT DATA { ";

    for (const auto & element : triples) {
        ss << element;
        ss << " . ";
    }

    ss << "}";
    return ss.str();
}

// _________________________________________________________________________________________________
std::string
olu::sparql::QueryWriter::writeDeleteQuery(const std::set<id_t> &ids, const std::string &osmTag) {
    std::ostringstream ss;
    ss << "DELETE { ?s ?p1 ?o1 . ?o1 ?p2 ?o2 . } WHERE { VALUES ?s { ";

    for (const auto & id : ids) {
        ss << osmTag;
        ss << ":";
        ss << std::to_string(id);
        ss << " ";
    }

    ss << "} ?s ?p1 ?o1 . OPTIONAL { ?o1 ?p2 ?o2. } }";
    return ss.str();
}

// _________________________________________________________________________________________________
std::string
olu::sparql::QueryWriter::writeQueryForNodeLocations(const std::set<id_t> &nodeIds) {
    std::ostringstream ss;
    ss << "SELECT ?nodeGeo ?location WHERE { VALUES ?nodeGeo { ";

    for (const auto & nodeId : nodeIds) {
        ss << "osm2rdfgeom:osm_node_";
        ss << std::to_string(nodeId);
        ss << " ";
    }

    ss << "} ?nodeGeo geo:asWKT ?location . }";
    return ss.str();
}

// _________________________________________________________________________________________________
std::string olu::sparql::QueryWriter::writeQueryForLatestNodeTimestamp() {
    std::string query = "SELECT ?p WHERE { ?s rdf:type osm:node . ?s osmmeta:timestamp ?p . } "
                        "ORDER BY DESC(?p) LIMIT 1";
    return query;
}

// _________________________________________________________________________________________________
std::string olu::sparql::QueryWriter::writeQueryForRelations(const std::set<id_t> & relationIds) {
    std::ostringstream ss;
    ss << "SELECT ?rel ?type"
          "(GROUP_CONCAT(?memberUri; separator=\";\") AS ?memberUris) "
          "(GROUP_CONCAT(?memberRole; separator=\";\") AS ?memberRoles) "
          "(GROUP_CONCAT(?memberPos; separator=\";\") AS ?memberPositions) "
          "WHERE { VALUES ?rel { ";

    for (const auto & relId : relationIds) {
        ss << "osmrel:";
        ss << std::to_string(relId);
        ss << " ";
    }

    ss << "} ?rel osmkey:type ?type . "
          "?rel osmrel:member ?o . "
          "?o osm2rdfmember:id ?memberUri . "
          "?o osm2rdfmember:role ?memberRole . "
          "?o osm2rdfmember:pos ?memberPos . "
          "} GROUP BY ?rel ?type";
    return ss.str();
}

// _________________________________________________________________________________________________
std::string olu::sparql::QueryWriter::writeQueryForWaysMembers(const std::set<id_t> &wayIds) {
    std::ostringstream ss;
    ss << "SELECT ?way "
          "(GROUP_CONCAT(?nodeUri; separator=\";\") AS ?nodeUris) "
          "(GROUP_CONCAT(?nodePos; separator=\";\") AS ?nodePositions) "
          "WHERE { VALUES ?way { ";

    for (const auto & wayId : wayIds) {
        ss << "osmway:";
        ss << std::to_string(wayId);
        ss << " ";
    }

    ss << "} ?way osmway:node ?member . "
          "?member osmway:node ?nodeUri . "
          "?member osm2rdfmember:pos ?nodePos "
          "} GROUP BY ?way";
    return ss.str();
}

// _________________________________________________________________________________________________
std::string olu::sparql::QueryWriter::writeQueryForReferencedNodes(const std::set<id_t> &wayIds) {
    std::ostringstream ss;
    ss << "SELECT ?node WHERE { VALUES ?way { ";

    for (const auto & wayId : wayIds) {
        ss << "osmway:" + std::to_string(wayId)+ " ";
    }

    ss << "} ?way osmway:node ?member . ?member osmway:node ?node . } GROUP BY ?node";
    return ss.str();
}

// _________________________________________________________________________________________________
std::string olu::sparql::QueryWriter::writeQueryForRelationMembers(const std::set<id_t> &relIds) {
    std::ostringstream ss;
    ss << "SELECT ?p WHERE { VALUES ?rel { ";

    for (const auto & relId : relIds) {
        ss << "osmrel:";
        ss << std::to_string(relId);
        ss << " ";
    }

    ss << "} ?rel osmrel:member ?o . ?o osm2rdfmember:id ?p . } GROUP BY ?p";
    return ss.str();
}

// _________________________________________________________________________________________________
std::string
olu::sparql::QueryWriter::writeQueryForWaysReferencingNodes(const std::set<id_t> &nodeIds) {
    std::ostringstream ss;
    ss << "SELECT ?way WHERE { VALUES ?node { ";

    for (const auto & nodeId : nodeIds) {
        ss << "osmnode:";
        ss << std::to_string(nodeId);
        ss << " ";
    }

    ss << "} ?identifier osmway:node ?node . ?way osmway:node ?identifier . } GROUP BY ?way";
    return ss.str();
}

// _________________________________________________________________________________________________
std::string
olu::sparql::QueryWriter::writeQueryForRelationsReferencingNodes(const std::set<id_t> &nodeIds) {
    std::ostringstream ss;
    ss << "SELECT ?s WHERE { VALUES ?node { ";

    for (const auto & nodeId : nodeIds) {
        ss << "osmnode:";
        ss << std::to_string(nodeId);
        ss << " ";
    }

    ss << "} ?s osmrel:member ?o . ?o osm2rdfmember:id ?node . } GROUP BY ?s";
    return ss.str();
}

// _________________________________________________________________________________________________
std::string
olu::sparql::QueryWriter::writeQueryForRelationsReferencingWays(const std::set<id_t> &wayIds) {
    std::ostringstream ss;
    ss << "SELECT ?s WHERE { VALUES ?way { ";

    for (const auto & wayId : wayIds) {
        ss << "osmway:";
        ss << std::to_string(wayId);
        ss << " ";
    }

    ss << "} ?s osmrel:member ?o . ?o osm2rdfmember:id ?way . } GROUP BY ?s";
    return ss.str();
}

// _________________________________________________________________________________________________
std::string
olu::sparql::QueryWriter::writeQueryForRelationsReferencingRelations(const std::set<id_t> &relationIds) {
    std::ostringstream ss;
    ss << "SELECT ?s WHERE { VALUES ?rel { ";

    for (const auto & relId : relationIds) {
        ss << "osmrel:";
        ss << std::to_string(relId);
        ss << " ";
    }

    ss << "} ?s osmrel:member ?o . "
          "?o osm2rdfmember:id ?rel . } "
          "GROUP BY ?s";
    return ss.str();
}

std::string olu::sparql::QueryWriter::writeQueryForTagsAndTimestamp(const std::string &subject) {
    std::string query = "SELECT ?key ?value ?time WHERE { "
                         "{ " + subject + " ?key ?value . "
                         "FILTER regex(str(?key), \"https://www.openstreetmap.org/wiki/Key:\") } "
                         "UNION { " + subject + " osmmeta:timestamp ?time } }";

    return query;
}

