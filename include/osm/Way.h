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

#ifndef OSM_LIVE_UPDATES_WAY_H
#define OSM_LIVE_UPDATES_WAY_H

#include "util/Types.h"

#include <string>
#include <vector>

namespace olu::osm {

    class Way {
    public:
        explicit Way(const id_t id): id(id) {};

        void setTimestamp(std::string const& timestamp);

        void addMember(id_t nodeId);
        void addTag(const std::string& key, const std::string& value);

        /**
        * Returns an osm xml element for a way with an id and node references.
        *
        * @example For wayId: `1` and nodeRefs: `{1,2,3}` the function
        * would return: `<way id="1"><nd ref="1"/><nd ref="2"/><nd ref="3"/></way>`
        */
        [[nodiscard]] std::string getXml() const;

        std::vector<id_t> getMembers() { return members; }
        [[nodiscard]] id_t getId() const { return id; }
        std::vector<KeyValue> getTags() { return tags; }
        std::string getTimestamp() { return timestamp; }
    protected:
        id_t id;
        std::string timestamp;
        std::vector<id_t> members;
        std::vector<KeyValue> tags;
    };

    /**
     * Exception that can appear inside the `Node` class.
     */
    class WayException final : public std::exception {
        std::string message;
    public:
        explicit WayException(const char* msg) : message(msg) { }

        [[nodiscard]] const char* what() const noexcept override {
            return message.c_str();
        }
    };

}

#endif //OSM_LIVE_UPDATES_WAY_H
