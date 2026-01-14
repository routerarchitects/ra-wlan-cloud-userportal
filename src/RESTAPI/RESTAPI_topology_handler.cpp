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
    "boardId": "917dfb3b-8d8f-4b2f-9a78-080c7a124ee9",
    "timestamp": "2025-12-08T12:30:22Z",
    "nodes": [
        {
            "serial": "f0090d2db49c",
            "uptime": 1434,
            "aps": [
                {
                    "bssid": "f0:09:0d:2d:b4:9c",
                    "ssid": "mangoWiFi",
                    "band": "2",
                    "channel": 6,
                    "mode": "ap",
                    "clients": null,
                    "timestamp": "2025-12-08T17:55:17+05:30"
                },
                {
                    "bssid": "dc:62:79:65:23:33",
                    "ssid": "mangoWiFi",
                    "band": "5",
                    "channel": 100,
                    "mode": "ap",
                    "clients": [
                        {
                            "station": "e6:8e:08:34:dd:a4",
                            "rssi": -33,
                            "connected": 743,
                            "inactive": 28,
                            "rx_rate_bitrate": 6000,
                            "tx_rate_bitrate": 680600,
                            "rx_rate_chwidth": 80
                        },
                        {
                            "station": "d6:7b:18:db:36:ba",
                            "rssi": -31,
                            "connected": 734,
                            "inactive": 7,
                            "rx_rate_bitrate": 24000,
                            "tx_rate_bitrate": 680600,
                            "rx_rate_chwidth": 80
                        },
                        {
                            "station": "f4:30:8b:af:d4:df",
                            "rssi": -42,
                            "connected": 695,
                            "inactive": 5,
                            "rx_rate_bitrate": 6000,
                            "tx_rate_bitrate": 433300,
                            "rx_rate_chwidth": 20
                        },
                        {
                            "station": "56:f7:01:c4:93:38",
                            "rssi": -45,
                            "connected": 501,
                            "inactive": 0,
                            "rx_rate_bitrate": 24000,
                            "tx_rate_bitrate": 1020600,
                            "rx_rate_chwidth": 20
                        }
                    ],
                    "timestamp": "2025-12-08T17:55:17+05:30"
                }
            ],
            "mesh": [
                {
                    "bssid": "de:62:79:65:23:33",
                    "ssid": "backhaul-mesh",
                    "band": "5",
                    "channel": 100,
                    "mode": "mesh",
                    "clients": [
                        {
                            "station": "de:62:79:65:30:7d",
                            "rssi": -57,
                            "connected": 1749,
                            "inactive": 0,
                            "rx_rate_bitrate": 864800,
                            "tx_rate_bitrate": 816700,
                            "rx_rate_chwidth": 80
                        }
                    ],
                    "timestamp": "2025-12-08T17:55:17+05:30"
                }
            ]
        },
        {
            "serial": "dc627965307e",
            "uptime": 1605,
            "aps": [
                {
                    "bssid": "dc:62:79:65:30:7e",
                    "ssid": "mangoWiFi",
                    "band": "2",
                    "channel": 6,
                    "mode": "ap",
                    "clients": null,
                    "timestamp": "2025-12-08T17:57:33+05:30"
                },
                {
                    "bssid": "dc:62:79:65:30:7d",
                    "ssid": "mangoWiFi",
                    "band": "5",
                    "channel": 100,
                    "mode": "ap",
                    "clients": [
                        {
                            "station": "ea:e9:78:61:09:a9",
                            "rssi": -46,
                            "connected": 841,
                            "inactive": 1,
                            "rx_rate_bitrate": 6000,
                            "tx_rate_bitrate": 680600,
                            "rx_rate_chwidth": 80
                        },
                        {
                            "station": "7a:ae:d8:a1:df:df",
                            "rssi": -48,
                            "connected": 667,
                            "inactive": 10,
                            "rx_rate_bitrate": 6000,
                            "tx_rate_bitrate": 453700,
                            "rx_rate_chwidth": 80
                        }
                    ],
                    "timestamp": "2025-12-08T17:57:33+05:30"
                }
            ],
            "mesh": [
                {
                    "bssid": "de:62:79:65:30:7d",
                    "ssid": "backhaul-mesh",
                    "band": "5",
                    "channel": 100,
                    "mode": "mesh",
                    "clients": [
                        {
                            "station": "de:62:79:65:23:33",
                            "rssi": -57,
                            "connected": 1886,
                            "inactive": 0,
                            "rx_rate_bitrate": 864800,
                            "tx_rate_bitrate": 272200,
                            "rx_rate_chwidth": 80
                        }
                    ],
                    "timestamp": "2025-12-08T17:57:33+05:30"
                }
            ]
        }
    ],
    "edges": {
        "wired": [],
        "mesh": [
            {
                "from": "f0090d2db49c",
                "to": "dc627965307e",
                "ssid": "backhaul-mesh",
                "band": "5",
                "channel": 100
            },
            {
                "from": "dc627965307e",
                "to": "f0090d2db49c",
                "ssid": "backhaul-mesh",
                "band": "5",
                "channel": 100
            }
        ]
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
