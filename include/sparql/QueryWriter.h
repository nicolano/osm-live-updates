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

#ifndef OSM_LIVE_UPDATES_QUERYWRITER_H
#define OSM_LIVE_UPDATES_QUERYWRITER_H

#include "util/Types.h"
#include "config/Config.h"

#include <string>
#include <vector>
#include <set>

namespace olu::sparql {

    /**
     *  Convenience class for some functions that return SPARQL queries.
     */
    class QueryWriter {
    public:
        explicit QueryWriter(config::Config  config): _config(std::move(config)) { }

        /**
         * @returns A SPARQL query that inserts a list of triples in to the database
         */
        [[nodiscard]] std::string writeInsertQuery(const std::vector<std::string>& triples) const;

        /**
         * @returns A SPARQL query that delete all triples with subject `osmTag:id` and all triples
         * that are linked via another node
         */
        [[nodiscard]] std::string writeDeleteQuery(const std::set<id_t> &ids, const std::string &osmTag) const;

        /**
        * @returns A SPARQL query for the locations of the nodes with the given ID in WKT format
        */
        [[nodiscard]] std::string writeQueryForNodeLocations(const std::set<id_t> &nodeIds) const;

        /**
         * @returns A SPARQL query for the latest timestamp of any node in the database
         */
        [[nodiscard]] std::string writeQueryForLatestNodeTimestamp() const;

        /**
        * @returns A SPARQL query for the subject of all members of the given relation
        */
        [[nodiscard]] std::string writeQueryForRelations(const std::set<id_t> & relationIds) const;

        /**
        * @returns A SPARQL query for the subject of all members of the given relation
        */
        [[nodiscard]] std::string writeQueryForWaysMembers(const std::set<id_t> &wayIds) const;

        /**
         * @returns A SPARQL query for all nodes that are referenced by the given way
         */
        [[nodiscard]] std::string writeQueryForReferencedNodes(const std::set<id_t> &wayIds) const;

        /**
         * @returns A SPARQL query for all members of the given relations
         */
        [[nodiscard]] std::string writeQueryForRelationMembers(const std::set<id_t> &relIds) const;

        /**
        * @returns A SPARQL query for all ways that reference the given nodes
        */
        [[nodiscard]] std::string writeQueryForWaysReferencingNodes(const std::set<id_t> &nodeIds) const;

        /**
        * @returns A SPARQL query for relations that reference the given nodes
        */
        [[nodiscard]] std::string writeQueryForRelationsReferencingNodes(const std::set<id_t> &nodeIds) const;

        /**
        * @returns A SPARQL query for relations that reference the given ways
        */
        [[nodiscard]] std::string writeQueryForRelationsReferencingWays(const std::set<id_t> &wayIds) const;

        /**
        * @returns A SPARQL query for relations that reference the given relations
        */
        [[nodiscard]] std::string writeQueryForRelationsReferencingRelations(const std::set<id_t> &relationIds) const;

        /**
        * @returns A SPARQL query for tags and timestamp of the given subject
        */
        [[nodiscard]] std::string writeQueryForTagsAndTimestamp(const std::string &subject) const;

        private:
        config::Config _config;

        [[nodiscard]] std::string getFromClauseOptional() const;

        [[nodiscard]] std::string wrapWithGraphOptional(const std::string& clause) const;
    };
} // namespace olu::sparql

#endif //OSM_LIVE_UPDATES_QUERYWRITER_H
