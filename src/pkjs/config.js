module.exports = [
  {
    "type": "heading",
    "defaultValue": "Satellite Tracker Settings"
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Satellite"
      },
      {
        "type": "input",
        "messageKey": "NoradId",
        "defaultValue": "25544",
        "label": "NORAD Catalog ID",
        "attributes": { "type": "number", "min": "1", "max": "99999" }
      },
      {
        "type": "text",
        "defaultValue": "Default 25544 = ISS (ZARYA). Find other IDs at celestrak.org."
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Display"
      },
      {
        "type": "toggle",
        "messageKey": "ShowLatLon",
        "label": "Show Latitude/Longitude",
        "defaultValue": true
      },
      {
        "type": "toggle",
        "messageKey": "ShowAltitude",
        "label": "Show Altitude",
        "defaultValue": true
      },
      {
        "type": "toggle",
        "messageKey": "ShowTrail",
        "label": "Show Orbit Trail",
        "defaultValue": false
      },
      {
        "type": "toggle",
        "messageKey": "ShowDayNight",
        "label": "Show Day/Night",
        "defaultValue": false
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save Settings"
  }
];
