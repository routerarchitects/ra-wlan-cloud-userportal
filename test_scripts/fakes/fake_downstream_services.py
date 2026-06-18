import http.server
import json
import os
import sys
import uuid
import re
from urllib.parse import urlparse, parse_qs

import psycopg2

# Track the last configure payload
last_configure_payload = None

current_scenario = "normal"
observations = []

def init_db():
    print("Initializing Postgres connection for fake server...")
    conn = psycopg2.connect(host="localhost", database="fakeprov", user="root", password="password")
    conn.autocommit = True
    cursor = conn.cursor()
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS mock_groups (
            id UUID PRIMARY KEY,
            subscriber_id TEXT,
            name TEXT,
            description TEXT
        )
    """)
    return conn

# Let this throw an exception and crash the server if Postgres is not available
db_conn = init_db()

class FakeHandler(http.server.BaseHTTPRequestHandler):
    def record_call(self):
        if not self.path.startswith("/observations") and not self.path.startswith("/reset-observations") and not self.path.startswith("/reset-db") and not self.path.startswith("/set-scenario") and "validate" not in self.path:
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

        if "/api/v1/subscribers/" in self.path and "/groups" in self.path:
            if current_scenario == "pc-404":
                self.send_response(404)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"not_found","message":"group not found"}).encode())
                return
            
            # Use Postgres for Groups CRUD
            cursor = db_conn.cursor()
            match = re.search(r'/groups/([a-f0-9\-]+)', self.path)
            if match:
                group_id = match.group(1)
                cursor.execute("SELECT id, name, description FROM mock_groups WHERE id = %s", (group_id,))
                row = cursor.fetchone()
                if row:
                    self.send_response(200)
                    self.end_headers()
                    self.wfile.write(json.dumps({"id": str(row[0]), "name": row[1], "description": row[2]}).encode())
                else:
                    self.send_response(404)
                    self.end_headers()
                    self.wfile.write(json.dumps({"error":"not_found","message":"group not found"}).encode())
            else:
                cursor.execute("SELECT id, name, description FROM mock_groups WHERE subscriber_id = 'sub1'")
                rows = cursor.fetchall()
                groups = [{"id": str(r[0]), "name": r[1], "description": r[2]} for r in rows]
                self.send_response(200)
                self.end_headers()
                self.wfile.write(json.dumps(groups).encode())
            return

        # Gateway/Prov fakes remain scenario-based and hardcoded
        if "/api/v1/subscriberDevice" in self.path:
            if current_scenario in ["prov-502", "delete-config-raw-prov-502"]:
                self.send_response(502)
                self.end_headers()
                self.wfile.write(json.dumps({
                    "error": "bad_gateway",
                    "message": "fake prov failure"
                }).encode())
                return

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
            if current_scenario == "delete-config-raw-gw-get-502":
                self.send_response(502)
                self.end_headers()
                self.wfile.write(json.dumps({"error": "bad_gateway", "message": "fake gw get failure"}).encode())
                return

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
            
        if "/reset-db" in self.path:
            cursor = db_conn.cursor()
            cursor.execute("TRUNCATE TABLE mock_groups")
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
                
            # DB-backed POST
            req_data = json.loads(body.decode())
            new_id = str(uuid.uuid4())
            cursor = db_conn.cursor()
            cursor.execute(
                "INSERT INTO mock_groups (id, subscriber_id, name, description) VALUES (%s, %s, %s, %s)",
                (new_id, "sub1", req_data.get("name"), req_data.get("description", ""))
            )
            self.send_response(200)
            self.end_headers()
            self.wfile.write(json.dumps({"id": new_id, "name": req_data.get("name"), "description": req_data.get("description", "")}).encode())
            return

        self.send_response(404)
        self.end_headers()

    def do_PUT(self):
        self.record_call()
        global current_scenario
        
        content_length = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(content_length) if content_length > 0 else b""

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
                
            # DB-backed PUT
            match = re.search(r'/groups/([a-f0-9\-]+)', self.path)
            if match:
                group_id = match.group(1)
                req_data = json.loads(body.decode())
                cursor = db_conn.cursor()
                cursor.execute("UPDATE mock_groups SET name = %s, description = %s WHERE id = %s", 
                               (req_data.get("name"), req_data.get("description", ""), group_id))
                if cursor.rowcount > 0:
                    self.send_response(200)
                    self.end_headers()
                    self.wfile.write(json.dumps({"id": group_id, "name": req_data.get("name"), "description": req_data.get("description", "")}).encode())
                else:
                    self.send_response(404)
                    self.end_headers()
                    self.wfile.write(json.dumps({"error":"not_found","message":"group not found"}).encode())
            else:
                self.send_response(404)
                self.end_headers()
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
                
            # DB-backed DELETE
            match = re.search(r'/groups/([a-f0-9\-]+)', self.path)
            if match:
                group_id = match.group(1)
                cursor = db_conn.cursor()
                cursor.execute("DELETE FROM mock_groups WHERE id = %s", (group_id,))
                if cursor.rowcount == 0 and current_scenario == "normal":
                    self.send_response(404)
                    self.end_headers()
                    self.wfile.write(json.dumps({"error":"not_found","message":"group not found"}).encode())
                    return

            if current_scenario.startswith("config-raw") or current_scenario.startswith("delete-config-raw"):
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
                self.send_response(200)
                self.end_headers()
                self.wfile.write(b"")
            return

        self.send_response(404)
        self.end_headers()
        
    def log_message(self, format, *args):
        pass

if __name__ == "__main__":
    import threading
    server = http.server.HTTPServer(('127.0.0.1', 8080), FakeHandler)
    server.serve_forever()
