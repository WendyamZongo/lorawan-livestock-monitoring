/**
 * ChirpStack Payload Decoder
 * Device: ESP32-C6 Cow Temperature + GPS Node
 * 
 * Payload format (11 bytes):
 * Bytes 0-1  : Temperature (int16 × 100)
 * Bytes 2-5  : Latitude (int32 × 10000)
 * Bytes 6-9  : Longitude (int32 × 10000)
 * Byte  10   : GNSS fix valid (1 = valid, 0 = no fix this cycle)
 */

function decodeUplink(input) {
  var bytes = input.bytes;

  // Decode temperature
  var tempInt = (bytes[0] << 8) | bytes[1];
  if (tempInt > 32767) tempInt -= 65536;
  var temperature = tempInt / 100.0;

  // Decode latitude
  var lat = (bytes[2] << 24) | (bytes[3] << 16) | (bytes[4] << 8) | bytes[5];
  if (lat > 2147483647) lat -= 4294967296;
  var latitude = lat / 10000.0;

  // Decode longitude
  var lon = (bytes[6] << 24) | (bytes[7] << 16) | (bytes[8] << 8) | bytes[9];
  if (lon > 2147483647) lon -= 4294967296;
  var longitude = lon / 10000.0;

  // GNSS fix validity flag sent by the node
  var gpsValid = (bytes[10] === 1);

  // Temperature status
  var status = "Normal";
  if (temperature < 35.0) status = "Too Low";
  else if (temperature <= 38.5) status = "Normal";
  else if (temperature <= 39.5) status = "Warning";
  else status = "FEVER";

  return {
    data: {
      temperature: temperature,
      latitude: latitude,
      longitude: longitude,
      gps_valid: gpsValid,
      status: status,
      google_maps: "https://www.google.com/maps?q=" + latitude + "," + longitude
    }
  };
}

function encodeDownlink(input) {
  return {
    fPort: 1,
    bytes: [0]
  };
}
