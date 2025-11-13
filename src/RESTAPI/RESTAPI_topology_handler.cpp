/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include "RESTAPI_topology_handler.h"
#include "Poco/JSON/Object.h"
#include "Poco/JSON/Parser.h"
namespace OpenWifi {
namespace {
const std::string kSampleTopologyJson{R"lit(
{
    "boardId": "5591dbe0-f836-4a92-83a2-d5c0b75b10ac",
    "timestamp": "2025-11-12T06:58:09Z",
    "nodes": [
        {
            "serial": "AA:BB:CC:DD:EE:FF",
            "aps": [
                {
                    "bssid": "dc:62:79:65:23:34",
                    "ssid": "TestSSID-2G",
                    "band": "2",
                    "channel": 6,
                    "mode": "ap",
                    "clients": [
                        {
                            "station": "fe:d1:47:83:81:61",
                            "rssi": -40,
                            "connected": 3551,
                            "inactive": 8,
                            "rx_rate_bitrate": 6000,
                            "tx_rate_bitrate": 65000,
                            "rx_rate_chwidth": 20
                        }
                    ],
                    "timestamp": "2025-11-12T12:28:09+05:30"
                },
                {
                    "bssid": "dc:62:79:65:23:33",
                    "ssid": "TestSSID-5G",
                    "band": "5",
                    "channel": 136,
                    "mode": "ap",
                    "clients": null,
                    "timestamp": "2025-11-12T12:28:09+05:30"
                }
            ],
            "mesh": []
        }
    ],
    "edges": {
        "wired": [],
        "mesh": []
    },
    "external": []
}
)lit"};
}

void RESTAPI_topology_handler::DoGet() {
    auto boardId = GetParameter("boardId");
    if (boardId.empty()) {
        boardId = GetParameter("boradID");
    }
    if (boardId.empty()) {
        return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters, "boardId");
    }

    // TODO: In a real implementation, you would fetch the topology data for the given boardId from network-topology service.
    Poco::JSON::Parser parser;
    auto root = parser.parse(kSampleTopologyJson).extract<Poco::JSON::Object::Ptr>();
    root->set("boardId", boardId);
    ReturnObject(*root);
}

} // namespace OpenWifi
