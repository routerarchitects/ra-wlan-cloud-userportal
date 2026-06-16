import http.server
import json
import os
import sys
from urllib.parse import urlparse, parse_qs

# Track the last configure payload
last_configure_payload = None

current_scenario = "normal"
observations = []

class FakeHandler(http.server.BaseHTTPRequestHandler):
    def record_call(self):
        if not self.path.startswith("/observations") and not self.path.startswith("/reset-observations") and not self.path.startswith("/set-scenario") and "validate" not in self.path:
            observations.append({"method": self.command, "path": self.path})

    def do_GET(self):
        self.record_call()
        global current_scenario
        
        # Fake OWSEC Auth
        if "validateSubToken" in self.path or "validateToken" in self.path:
            parsed = urlparse(self.path)
            qs = parse_qs(parsed.query)
            token = qs.get("token", [""])[0]

            if not token or token == "bad-token":
                self.send_response(401)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(json.dumps({"error": "invalid_token"}).encode())
                return

            if token == "dummy-test-token":
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                res = {
                "tokenInfo": {
                    "access_token": "dummy-test-token",
                    "token_type": "Bearer",
                    "expires_in": 3600,
                    "created": 2000000000,
                    "username": "test@test.com"
                },
                "userInfo": {
                    "id": "sub1",
                    "name": "Test User",
                    "description": "",
                    "avatar": "",
                    "email": "test@test.com",
                    "validated": True,
                    "validationEmail": "",
                    "validationDate": 0,
                    "creationDate": 0,
                    "validationURI": "",
                    "changePassword": False,
                    "lastLogin": 0,
                    "currentLoginURI": "",
                    "lastPasswordChange": 0,
                    "lastEmailCheck": 0,
                    "waitingForEmailCheck": False,
                    "locale": "",
                    "notes": [],
                    "location": "",
                    "owner": "operator1",
                    "suspended": False,
                    "blackListed": False,
                    "userRole": "subscriber",
                    "userTypeProprietaryInfo": {
                        "mobiles": [],
                        "mfa": {"enabled": False, "method": ""},
                        "authenticatorSecret": ""
                    },
                    "securityPolicy": "",
                    "securityPolicyChange": 0,
                    "currentPassword": "",
                    "lastPasswords": [],
                    "oauthType": "",
                    "oauthUserInfo": "",
                    "modified": 0,
                    "signingUp": ""
                }
                }
                self.wfile.write(json.dumps(res).encode())
                return

            self.send_response(401)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({"error": "invalid_token"}).encode())
            return

        if "/api/v1/subscribers/" in self.path and "/groups" in self.path:
            # Parental Control GET
            if current_scenario == "pc-404":
                self.send_response(404)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"not_found","message":"group not found"}).encode())
                return
            
            if self.path.endswith("/groups"):
                # Scenario A: list success
                self.send_response(200)
                self.end_headers()
                self.wfile.write(json.dumps([{"id": "11111111-1111-4111-8111-111111111111", "name": "Fake Group", "description": "desc"}]).encode())
            else:
                # Scenario C: get-by-id success
                self.send_response(200)
                self.end_headers()
                self.wfile.write(json.dumps({"id": "11111111-1111-4111-8111-111111111111", "name": "Fake Group", "description": "desc"}).encode())
            return
            
        if "inventory" in self.path or "subscriberDevice" in self.path or "subscriber" in self.path:
            if current_scenario in ["prov-502", "delete-config-raw-prov-502"]:
                self.send_response(502)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"bad_gateway","message":"fake prov failure"}).encode())
                return
                
            # Provisioning GET devices
            # Needs to return an OLG device
            self.send_response(200)
            self.end_headers()
            res = {
                "subscriberDevices": [
                    {
                        "info": {
                            "id": "dev1",
                            "name": "Test Device",
                            "description": "",
                            "notes": []
                        },
                        "serialNumber": "AA:BB:CC:DD:EE:FF",
                        "deviceGroup": "OLG",
                        "deviceType": "gateway",
                        "operatorId": "operator1",
                        "subscriberId": "sub1",
                        "location": {"id": "", "name": ""},
                        "contact": {"id": "", "name": ""},
                        "managementPolicy": "",
                        "serviceClass": "",
                        "qrCode": "",
                        "geoCode": "",
                        "deviceRules": {},
                        "state": "",
                        "locale": "",
                        "billingCode": "",
                        "configuration": [],
                        "suspended": False,
                        "realMacAddress": ""
                    }
                ]
            }
            self.wfile.write(json.dumps(res).encode())
            return

        if "/api/v1/device/" in self.path and "configure" not in self.path and "statistics" not in self.path:
            if current_scenario in ["gw-get-malformed", "delete-config-raw-gw-get-malformed"]:
                self.send_response(200)
                self.end_headers()
                self.wfile.write(json.dumps({"configuration_broken": True}).encode())
                return
                
            # Gateway GET config
            self.send_response(200)
            self.end_headers()
            res = {
                "configuration": {
                    "config-raw": [
                        ["set", "wifi.ssid", "RouterTestSSID"],
                        ["set", "parental_control.old_rule", "enabled", "1"]
                    ]
                }
            }
            self.wfile.write(json.dumps(res).encode())
            return

        if self.path == "/observations":
            self.send_response(200)
            self.end_headers()
            res = {"calls": observations, "last_configure_payload": last_configure_payload}
            self.wfile.write(json.dumps(res).encode())
            return

        self.send_response(404)
        self.end_headers()

    def do_POST(self):
        self.record_call()
        global last_configure_payload
        global current_scenario
        global observations
        
        content_length = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(content_length) if content_length > 0 else b""
        
        if "/reset-observations" in self.path:
            observations.clear()
            last_configure_payload = None
            self.send_response(200)
            self.end_headers()
            return

        if "/set-scenario" in self.path:
            try:
                data = json.loads(body.decode())
                if "scenario" not in data:
                    raise ValueError("Missing scenario")
                current_scenario = data["scenario"]
                self.send_response(200)
                self.end_headers()
            except Exception as e:
                self.send_response(400)
                self.end_headers()
                self.wfile.write(str(e).encode())
            return
        
        if "configure" in self.path:
            if current_scenario in ["gw-configure-502", "delete-config-raw-gw-configure-502"]:
                self.send_response(502)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"bad_gateway","message":"fake gw configure failure"}).encode())
                return
                
            # Gateway POST configure
            last_configure_payload = json.loads(body.decode())
            self.send_response(200)
            self.end_headers()
            self.wfile.write(json.dumps({"status": "Success"}).encode())
            return

        if "/api/v1/subscribers/" in self.path and "/groups" in self.path:
            if current_scenario == "pc-409":
                self.send_response(409)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"conflict","message":"group conflict"}).encode())
                return
                
            # Scenario B: create success
            self.send_response(200)
            self.end_headers()
            self.wfile.write(json.dumps({"id": "22222222-2222-4222-8222-222222222222", "name": "Created Group", "description": "desc"}).encode())
            return

        self.send_response(404)
        self.end_headers()

    def do_PUT(self):
        self.record_call()
        global current_scenario

        if "/api/v1/subscribers/" in self.path and "/groups" in self.path:
            if current_scenario == "pc-404":
                self.send_response(404)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"not_found","message":"group not found"}).encode())
                return
            elif current_scenario == "pc-409":
                self.send_response(409)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"conflict","message":"group conflict"}).encode())
                return
                
            # Scenario D: update success
            self.send_response(200)
            self.end_headers()
            self.wfile.write(json.dumps({"id": "11111111-1111-4111-8111-111111111111", "name": "Updated Group", "description": "desc"}).encode())
            return

        self.send_response(404)
        self.end_headers()

    def do_DELETE(self):
        self.record_call()
        global current_scenario
        
        if "/api/v1/subscribers/" in self.path and "/groups" in self.path:
            if current_scenario == "pc-404":
                self.send_response(404)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"not_found","message":"group not found"}).encode())
                return
            elif current_scenario == "pc-409":
                self.send_response(409)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"conflict","message":"group conflict"}).encode())
                return
                
            if current_scenario.startswith("config-raw") or current_scenario.startswith("delete-config-raw"):
                # Scenario F: delete success with config-raw
                self.send_response(200)
                self.end_headers()
                res = {
                    "config-raw": [
                        ["delete", "parental_control.ci_rule"],
                        ["set", "parental_control.ci_rule", "enabled", "0"]
                    ]
                }
                self.wfile.write(json.dumps(res).encode())
            else:
                # Scenario E: delete success without config-raw
                self.send_response(200)
                self.end_headers()
                self.wfile.write(b"") # Empty body or standard success
            return

        self.send_response(404)
        self.end_headers()
        
    def log_message(self, format, *args):
        pass # Suppress logs for tests

if __name__ == "__main__":
    import threading
    server = http.server.HTTPServer(('127.0.0.1', 8080), FakeHandler)
    server.serve_forever()
