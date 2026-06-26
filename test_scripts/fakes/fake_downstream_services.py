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

VENUE_ID = "11111111-1111-4111-8111-111111111111"
BOARD_ID = "22222222-2222-4222-8222-222222222222"
ENTITY_ID = "33333333-3333-4333-8333-333333333333"
NIL_UUID = "00000000-0000-0000-0000-000000000000"

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
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS mock_schedules (
            id UUID PRIMARY KEY,
            subscriber_id TEXT,
            name TEXT,
            description TEXT,
            enabled BOOLEAN,
            action_type TEXT,
            target_kind TEXT,
            target_value TEXT,
            start_minute INTEGER,
            stop_minute INTEGER,
            weekdays INTEGER[]
        )
    """)
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS mock_group_devices (
            group_id UUID,
            client_mac TEXT,
            PRIMARY KEY (group_id, client_mac)
        )
    """)
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS mock_group_schedules (
            group_id UUID,
            schedule_id UUID,
            PRIMARY KEY (group_id, schedule_id)
        )
    """)
    return conn

# Let this throw an exception and crash the server if Postgres is not available
db_conn = init_db()

class FakeHandler(http.server.BaseHTTPRequestHandler):
    def record_call(self, body=None):
        if not self.path.startswith("/observations") and not self.path.startswith("/reset-observations") and not self.path.startswith("/reset-db") and not self.path.startswith("/set-scenario") and "validate" not in self.path:
            call_info = {"method": self.command, "path": self.path}
            if body is not None:
                try:
                    call_info["body"] = json.loads(body.decode())
                except Exception:
                    call_info["body"] = body.decode()
            observations.append(call_info)

    def do_GET(self):
        self.record_call()
        global current_scenario
        
        if "/api/v1/inventory/" in self.path:
            if current_scenario == "inventory-missing":
                self.send_response(404)
                self.end_headers()
                self.wfile.write(json.dumps({"error": "not_found", "message": "inventory not found"}).encode())
                return
            mac = self.path.split("/")[-1]
            self.send_response(200)
            self.end_headers()
            self.wfile.write(json.dumps({
                "serialNumber": mac,
                "venue": "" if current_scenario == "inventory-venue-empty" else VENUE_ID,
                "subscriber": "sub1"
            }).encode())
            return

        if "/api/v1/venue/" in self.path:
            if current_scenario == "venue-missing":
                self.send_response(404)
                self.end_headers()
                self.wfile.write(json.dumps({"error": "not_found", "message": "venue not found"}).encode())
                return
            if current_scenario == "venue-fail":
                self.send_response(500)
                self.end_headers()
                self.wfile.write(json.dumps({"error": "internal_error", "message": "venue lookup failed"}).encode())
                return
            self.send_response(200)
            self.end_headers()
            res = {
                "id": VENUE_ID,
                "name": "Test Venue",
                "description": "",
                "created": 0,
                "modified": 0,
                "notes": [],
                "tags": [],
                "parent": NIL_UUID,
                "entity": ENTITY_ID,
                "children": [],
                "devices": [],
                "topology": [],
                "design": "",
                "managementPolicy": NIL_UUID,
                "deviceConfiguration": [],
                "contacts": [],
                "location": "",
                "deviceRules": {
                    "rcOnly": "inherit",
                    "rrm": "inherit",
                    "firmwareUpgrade": "inherit"
                },
                "sourceIP": [],
                "variables": [],
                "managementPolicies": [],
                "managementRoles": [],
                "maps": [],
                "configurations": [],
                "boards": [] if current_scenario == "board-missing" else [BOARD_ID]
            }
            self.wfile.write(json.dumps(res).encode())
            return

        if "/api/v1/topology" in self.path:
            if current_scenario == "topology-fail":
                self.send_response(500)
                self.end_headers()
                self.wfile.write(json.dumps({"error": "internal_error", "message": "topology fetch failure"}).encode())
                return
            if current_scenario == "topology-malformed":
                self.send_response(200)
                self.end_headers()
                self.wfile.write(json.dumps({"historicalClients": "not-an-array"}).encode())
                return
            if current_scenario == "mac-not-present":
                self.send_response(200)
                self.end_headers()
                self.wfile.write(json.dumps({"historicalClients": [{"station": "11:22:33:44:55:66"}]}).encode())
                return

            self.send_response(200)
            self.end_headers()
            self.wfile.write(json.dumps({"historicalClients": [{"station": "aa:bb:cc:dd:ee:ff"}]}).encode())
            return

        if "/api/v1/subscribers/" in self.path and "/groups/" in self.path and "/devices" in self.path:
            if current_scenario == "pc-404":
                self.send_response(404)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"not_found","message":"not found"}).encode())
                return
            parts = self.path.split("/")
            group_id = parts[6]
            cursor = db_conn.cursor()
            cursor.execute("SELECT id FROM mock_groups WHERE id = %s", (group_id,))
            if not cursor.fetchone():
                self.send_response(404)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"not_found","message":"group not found"}).encode())
                return

            if len(parts) > 8:
                mac = parts[8]
                cursor.execute("SELECT client_mac FROM mock_group_devices WHERE group_id = %s AND client_mac = %s", (group_id, mac))
                row = cursor.fetchone()
                if row:
                    self.send_response(200)
                    self.end_headers()
                    self.wfile.write(json.dumps({"client_mac": row[0]}).encode())
                else:
                    self.send_response(404)
                    self.end_headers()
                    self.wfile.write(json.dumps({"error":"not_found","message":"device not linked to group"}).encode())
            else:
                cursor.execute("SELECT client_mac FROM mock_group_devices WHERE group_id = %s", (group_id,))
                rows = cursor.fetchall()
                devices = [{"client_mac": r[0]} for r in rows]
                self.send_response(200)
                self.end_headers()
                self.wfile.write(json.dumps(devices).encode())
            return

        if "/api/v1/subscribers/" in self.path and "/groups/" in self.path and "/schedules" in self.path:
            if current_scenario == "pc-404":
                self.send_response(404)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"not_found","message":"not found"}).encode())
                return
            parts = self.path.split("/")
            group_id = parts[6]
            cursor = db_conn.cursor()
            cursor.execute("SELECT id FROM mock_groups WHERE id = %s", (group_id,))
            if not cursor.fetchone():
                self.send_response(404)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"not_found","message":"group not found"}).encode())
                return

            if len(parts) > 8:
                sid = parts[8]
                cursor.execute("SELECT schedule_id FROM mock_group_schedules WHERE group_id = %s AND schedule_id = %s", (group_id, sid))
                row = cursor.fetchone()
                if row:
                    self.send_response(200)
                    self.end_headers()
                    self.wfile.write(json.dumps({"id": str(row[0])}).encode())
                else:
                    self.send_response(404)
                    self.end_headers()
                    self.wfile.write(json.dumps({"error":"not_found","message":"schedule not linked to group"}).encode())
            else:
                cursor.execute("SELECT schedule_id FROM mock_group_schedules WHERE group_id = %s", (group_id,))
                rows = cursor.fetchall()
                schedules = [{"id": str(r[0])} for r in rows]
                self.send_response(200)
                self.end_headers()
                self.wfile.write(json.dumps(schedules).encode())
            return

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

            if token in ["dummy-test-token", "no-owner-token"]:
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                owner_val = "" if token == "no-owner-token" else "operator1"
                res = {
                "tokenInfo": {
                    "access_token": token,
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
                    "owner": owner_val,
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

        if "/api/v1/subscribers/" in self.path and "/schedules" in self.path:
            if current_scenario == "pc-404":
                self.send_response(404)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"not_found","message":"schedule not found"}).encode())
                return
            
            cursor = db_conn.cursor()
            match = re.search(r'/schedules/([a-f0-9\-]+)', self.path)
            if match:
                schedule_id = match.group(1)
                cursor.execute("""
                    SELECT id, name, description, enabled, action_type, target_kind, target_value, start_minute, stop_minute, weekdays 
                    FROM mock_schedules WHERE id = %s
                """, (schedule_id,))
                row = cursor.fetchone()
                if row:
                    self.send_response(200)
                    self.end_headers()
                    self.wfile.write(json.dumps({
                        "id": str(row[0]),
                        "name": row[1],
                        "description": row[2],
                        "enabled": row[3],
                        "action_type": row[4],
                        "target_kind": row[5],
                        "target_value": row[6],
                        "start_minute": row[7],
                        "stop_minute": row[8],
                        "weekdays": row[9]
                    }).encode())
                else:
                    self.send_response(404)
                    self.end_headers()
                    self.wfile.write(json.dumps({"error":"not_found","message":"schedule not found"}).encode())
            else:
                cursor.execute("""
                    SELECT id, name, description, enabled, action_type, target_kind, target_value, start_minute, stop_minute, weekdays 
                    FROM mock_schedules WHERE subscriber_id = 'sub1'
                """)
                rows = cursor.fetchall()
                schedules = [{
                    "id": str(r[0]),
                    "name": r[1],
                    "description": r[2],
                    "enabled": r[3],
                    "action_type": r[4],
                    "target_kind": r[5],
                    "target_value": r[6],
                    "start_minute": r[7],
                    "stop_minute": r[8],
                    "weekdays": r[9]
                } for r in rows]
                self.send_response(200)
                self.end_headers()
                self.wfile.write(json.dumps(schedules).encode())
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
                        ["set", "parental_control.old_rule.enabled", "1"]
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
        global last_configure_payload
        global current_scenario
        global observations
        
        content_length = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(content_length) if content_length > 0 else b""
        
        self.record_call(body)
        
        if "/reset-observations" in self.path:
            observations.clear()
            last_configure_payload = None
            self.send_response(200)
            self.end_headers()
            return
            
        if "/reset-db" in self.path:
            cursor = db_conn.cursor()
            cursor.execute("TRUNCATE TABLE mock_groups")
            cursor.execute("TRUNCATE TABLE mock_schedules")
            cursor.execute("TRUNCATE TABLE mock_group_devices")
            cursor.execute("TRUNCATE TABLE mock_group_schedules")
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

        if "/api/v1/subscribers/" in self.path and "/groups/" in self.path and "/devices" in self.path:
            if current_scenario == "pc-404":
                self.send_response(404)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"not_found","message":"not found"}).encode())
                return
            elif current_scenario == "pc-409":
                self.send_response(409)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"conflict","message":"conflict"}).encode())
                return
            
            parts = self.path.split("/")
            group_id = parts[6]
            req_data = json.loads(body.decode())
            mac = req_data.get("client_mac")
            
            cursor = db_conn.cursor()
            cursor.execute("SELECT id FROM mock_groups WHERE id = %s", (group_id,))
            if not cursor.fetchone():
                self.send_response(404)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"not_found","message":"group not found"}).encode())
                return
                
            cursor.execute("SELECT client_mac FROM mock_group_devices WHERE group_id = %s AND client_mac = %s", (group_id, mac))
            if cursor.fetchone():
                self.send_response(409)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"conflict","message":"device already linked to group"}).encode())
                return

            cursor.execute("INSERT INTO mock_group_devices (group_id, client_mac) VALUES (%s, %s)", (group_id, mac))
            
            resp_obj = {"client_mac": mac}
            if current_scenario.startswith("config-raw") or current_scenario.startswith("delete-config-raw"):
                resp_obj["config-raw"] = [
                    ["set", "parental_control.ci_rule.enabled", "1"]
                ]
            self.send_response(200)
            self.end_headers()
            self.wfile.write(json.dumps(resp_obj).encode())
            return

        if "/api/v1/subscribers/" in self.path and "/groups/" in self.path and "/schedules" in self.path:
            if current_scenario == "pc-404":
                self.send_response(404)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"not_found","message":"not found"}).encode())
                return
            elif current_scenario == "pc-409":
                self.send_response(409)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"conflict","message":"conflict"}).encode())
                return
            
            parts = self.path.split("/")
            group_id = parts[6]
            req_data = json.loads(body.decode())
            sid = req_data.get("schedule_id")
            
            cursor = db_conn.cursor()
            cursor.execute("SELECT id FROM mock_groups WHERE id = %s", (group_id,))
            if not cursor.fetchone():
                self.send_response(404)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"not_found","message":"group not found"}).encode())
                return
                
            cursor.execute("SELECT id FROM mock_schedules WHERE id = %s", (sid,))
            if not cursor.fetchone():
                self.send_response(404)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"not_found","message":"schedule not found"}).encode())
                return
                
            cursor.execute("SELECT schedule_id FROM mock_group_schedules WHERE group_id = %s AND schedule_id = %s", (group_id, sid))
            if cursor.fetchone():
                self.send_response(409)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"conflict","message":"schedule already linked to group"}).encode())
                return

            cursor.execute("INSERT INTO mock_group_schedules (group_id, schedule_id) VALUES (%s, %s)", (group_id, sid))
            
            resp_obj = {"schedule_id": sid}
            if current_scenario.startswith("config-raw") or current_scenario.startswith("delete-config-raw"):
                resp_obj["config-raw"] = [
                    ["set", "parental_control.ci_rule.enabled", "1"]
                ]
            self.send_response(200)
            self.end_headers()
            self.wfile.write(json.dumps(resp_obj).encode())
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

        if "/api/v1/subscribers/" in self.path and "/schedules" in self.path:
            if current_scenario == "pc-409":
                self.send_response(409)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"conflict","message":"schedule conflict"}).encode())
                return
                
            req_data = json.loads(body.decode())
            new_id = str(uuid.uuid4())
            cursor = db_conn.cursor()
            cursor.execute("""
                INSERT INTO mock_schedules (id, subscriber_id, name, description, enabled, action_type, target_kind, target_value, start_minute, stop_minute, weekdays)
                VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s)
            """, (
                new_id, "sub1", req_data.get("name"), req_data.get("description"),
                req_data.get("enabled", True), req_data.get("action_type", "BLOCK"),
                req_data.get("target_kind"), req_data.get("target_value"),
                req_data.get("start_minute"), req_data.get("stop_minute"),
                req_data.get("weekdays")
            ))
            self.send_response(200)
            self.end_headers()
            self.wfile.write(json.dumps({
                "id": new_id,
                "name": req_data.get("name"),
                "description": req_data.get("description"),
                "enabled": req_data.get("enabled", True),
                "action_type": req_data.get("action_type", "BLOCK"),
                "target_kind": req_data.get("target_kind"),
                "target_value": req_data.get("target_value"),
                "start_minute": req_data.get("start_minute"),
                "stop_minute": req_data.get("stop_minute"),
                "weekdays": req_data.get("weekdays")
            }).encode())
            return

        self.send_response(404)
        self.end_headers()

    def do_PUT(self):
        global current_scenario
        
        content_length = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(content_length) if content_length > 0 else b""

        self.record_call(body)

        if "/api/v1/subscribers/" in self.path and "/groups/" in self.path and "/schedules" in self.path:
            if current_scenario == "pc-404":
                self.send_response(404)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"not_found","message":"not found"}).encode())
                return
            elif current_scenario == "pc-409":
                self.send_response(409)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"conflict","message":"conflict"}).encode())
                return
            
            parts = self.path.split("/")
            group_id = parts[6]
            req_data = json.loads(body.decode())
            sids = req_data.get("schedule_ids", [])
            
            cursor = db_conn.cursor()
            cursor.execute("SELECT id FROM mock_groups WHERE id = %s", (group_id,))
            if not cursor.fetchone():
                self.send_response(404)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"not_found","message":"group not found"}).encode())
                return
                
            for sid in sids:
                cursor.execute("SELECT id FROM mock_schedules WHERE id = %s", (sid,))
                if not cursor.fetchone():
                    self.send_response(404)
                    self.end_headers()
                    self.wfile.write(json.dumps({"error":"not_found","message": f"schedule {sid} not found"}).encode())
                    return
            
            cursor.execute("DELETE FROM mock_group_schedules WHERE group_id = %s", (group_id,))
            for sid in sids:
                cursor.execute("INSERT INTO mock_group_schedules (group_id, schedule_id) VALUES (%s, %s)", (group_id, sid))
                
            resp_obj = {"schedule_ids": sids}
            if current_scenario.startswith("config-raw") or current_scenario.startswith("delete-config-raw"):
                resp_obj["config-raw"] = [
                    ["set", "parental_control.ci_rule.enabled", "1"]
                ]
            self.send_response(200)
            self.end_headers()
            self.wfile.write(json.dumps(resp_obj).encode())
            return

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

        if "/api/v1/subscribers/" in self.path and "/schedules" in self.path:
            if current_scenario == "pc-404":
                self.send_response(404)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"not_found","message":"schedule not found"}).encode())
                return
            elif current_scenario == "pc-409":
                self.send_response(409)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"conflict","message":"schedule conflict"}).encode())
                return
                
            # DB-backed PUT
            match = re.search(r'/schedules/([a-f0-9\-]+)', self.path)
            if match:
                schedule_id = match.group(1)
                req_data = json.loads(body.decode())
                cursor = db_conn.cursor()
                cursor.execute("""
                    UPDATE mock_schedules 
                    SET name = %s, description = %s, enabled = %s, action_type = %s, target_kind = %s, target_value = %s, start_minute = %s, stop_minute = %s, weekdays = %s 
                    WHERE id = %s
                """, (
                    req_data.get("name"), req_data.get("description"),
                    req_data.get("enabled"), req_data.get("action_type"),
                    req_data.get("target_kind"), req_data.get("target_value"),
                    req_data.get("start_minute"), req_data.get("stop_minute"),
                    req_data.get("weekdays"), schedule_id
                ))
                if cursor.rowcount > 0:
                    resp_obj = {
                        "id": schedule_id,
                        "name": req_data.get("name"),
                        "description": req_data.get("description"),
                        "enabled": req_data.get("enabled"),
                        "action_type": req_data.get("action_type"),
                        "target_kind": req_data.get("target_kind"),
                        "target_value": req_data.get("target_value"),
                        "start_minute": req_data.get("start_minute"),
                        "stop_minute": req_data.get("stop_minute"),
                        "weekdays": req_data.get("weekdays")
                    }
                    if current_scenario == "config-raw-null":
                        resp_obj["config-raw"] = None
                    elif current_scenario == "config-raw-malformed":
                        resp_obj["config-raw"] = [123]
                    elif current_scenario.startswith("config-raw") or current_scenario.startswith("delete-config-raw"):
                        resp_obj["config-raw"] = [
                            ["set", "parental_control.ci_rule.enabled", "1"]
                        ]
                    self.send_response(200)
                    self.end_headers()
                    self.wfile.write(json.dumps(resp_obj).encode())
                else:
                    self.send_response(404)
                    self.end_headers()
                    self.wfile.write(json.dumps({"error":"not_found","message":"schedule not found"}).encode())
            else:
                self.send_response(404)
                self.end_headers()
            return

        self.send_response(404)
        self.end_headers()

    def do_DELETE(self):
        self.record_call()
        global current_scenario
        
        if "/api/v1/subscribers/" in self.path and "/groups/" in self.path and "/devices" in self.path:
            if current_scenario == "pc-404":
                self.send_response(404)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"not_found","message":"not found"}).encode())
                return
            
            parts = self.path.split("/")
            group_id = parts[6]
            mac = parts[8]
            
            cursor = db_conn.cursor()
            cursor.execute("SELECT id FROM mock_groups WHERE id = %s", (group_id,))
            if not cursor.fetchone():
                self.send_response(404)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"not_found","message":"group not found"}).encode())
                return
                
            cursor.execute("DELETE FROM mock_group_devices WHERE group_id = %s AND client_mac = %s", (group_id, mac))
            if cursor.rowcount == 0:
                self.send_response(404)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"not_found","message":"device not linked to group"}).encode())
                return
            
            resp_obj = {}
            if current_scenario.startswith("config-raw") or current_scenario.startswith("delete-config-raw"):
                resp_obj["config-raw"] = [
                    ["set", "parental_control.ci_rule.enabled", "0"]
                ]
            self.send_response(200)
            self.end_headers()
            self.wfile.write(json.dumps(resp_obj).encode())
            return

        if "/api/v1/subscribers/" in self.path and "/groups/" in self.path and "/schedules" in self.path:
            if current_scenario == "pc-404":
                self.send_response(404)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"not_found","message":"not found"}).encode())
                return
            
            parts = self.path.split("/")
            group_id = parts[6]
            sid = parts[8]
            
            cursor = db_conn.cursor()
            cursor.execute("SELECT id FROM mock_groups WHERE id = %s", (group_id,))
            if not cursor.fetchone():
                self.send_response(404)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"not_found","message":"group not found"}).encode())
                return
                
            cursor.execute("DELETE FROM mock_group_schedules WHERE group_id = %s AND schedule_id = %s", (group_id, sid))
            if cursor.rowcount == 0:
                self.send_response(404)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"not_found","message":"schedule not linked to group"}).encode())
                return
            
            resp_obj = {}
            if current_scenario.startswith("config-raw") or current_scenario.startswith("delete-config-raw"):
                resp_obj["config-raw"] = [
                    ["set", "parental_control.ci_rule.enabled", "0"]
                ]
            self.send_response(200)
            self.end_headers()
            self.wfile.write(json.dumps(resp_obj).encode())
            return

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

            if current_scenario == "config-raw-null":
                self.send_response(200)
                self.end_headers()
                self.wfile.write(json.dumps({"config-raw": None}).encode())
            elif current_scenario.startswith("config-raw") or current_scenario.startswith("delete-config-raw"):
                self.send_response(200)
                self.end_headers()
                res = {
                    "config-raw": [
                        ["set", "parental_control.ci_rule.enabled", "0"]
                    ]
                }
                self.wfile.write(json.dumps(res).encode())
            else:
                self.send_response(200)
                self.end_headers()
                self.wfile.write(b"")
            return

        if "/api/v1/subscribers/" in self.path and "/schedules" in self.path:
            if current_scenario == "pc-404":
                self.send_response(404)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"not_found","message":"schedule not found"}).encode())
                return
            elif current_scenario == "pc-409":
                self.send_response(409)
                self.end_headers()
                self.wfile.write(json.dumps({"error":"conflict","message":"schedule conflict"}).encode())
                return
                
            # DB-backed DELETE
            match = re.search(r'/schedules/([a-f0-9\-]+)', self.path)
            if match:
                schedule_id = match.group(1)
                cursor = db_conn.cursor()
                cursor.execute("DELETE FROM mock_schedules WHERE id = %s", (schedule_id,))
                if cursor.rowcount == 0 and current_scenario == "normal":
                    self.send_response(404)
                    self.end_headers()
                    self.wfile.write(json.dumps({"error":"not_found","message":"schedule not found"}).encode())
                    return

            if current_scenario == "config-raw-null":
                self.send_response(200)
                self.end_headers()
                self.wfile.write(json.dumps({"config-raw": None}).encode())
            elif current_scenario == "config-raw-malformed":
                self.send_response(200)
                self.end_headers()
                self.wfile.write(json.dumps({"config-raw": [123]}).encode())
            elif current_scenario.startswith("config-raw") or current_scenario.startswith("delete-config-raw"):
                self.send_response(200)
                self.end_headers()
                res = {
                    "config-raw": [
                        ["set", "parental_control.ci_rule.enabled", "0"]
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
