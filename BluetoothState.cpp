/*
 * Shared Bluetooth/PAN runtime state.
 *
 * This file owns the global arrays used by scan/audit/report screens. Other
 * files only use extern declarations from BluetoothState.h, which avoids
 * duplicate global storage.
 *
 * Expected outcome:
 *   - Scan pages store cleaned device names, addresses, RSSI, and metadata in a
 *     single shared result list.
 *
 * TODO / limitation:
 *   - Increase report capacity only if the OLED UI is also updated to handle
 *     paging cleanly.
 */

#include "BluetoothState.h"

// ================= BLUETOOTH OBJECT =================

BluetoothSerial SerialBT;

// ================= SHARED STATE DEFINITIONS =================

bool classicStarted = false;
bool bleStarted = false;

char btDeviceLabels[MAX_BT_DEVICES][BT_LABEL_LEN];
char btDeviceAddresses[MAX_BT_DEVICES][BT_ADDR_LEN];
char btDeviceTypes[MAX_BT_DEVICES][BT_TYPE_LEN];

int btDeviceRSSI[MAX_BT_DEVICES];
bool btDeviceHasRSSI[MAX_BT_DEVICES];

int btBleServiceUUIDCount[MAX_BT_DEVICES];
int btBleServiceDataCount[MAX_BT_DEVICES];
int btBleManufacturerDataLen[MAX_BT_DEVICES];
int btBlePayloadLen[MAX_BT_DEVICES];
int btBleAddressType[MAX_BT_DEVICES];

int btBleTxPower[MAX_BT_DEVICES];
bool btBleHasTxPower[MAX_BT_DEVICES];

int btBleAppearance[MAX_BT_DEVICES];
bool btBleHasAppearance[MAX_BT_DEVICES];

int btDeviceCount = 0;

char bluetoothResultItemBuffer[REPORT_LINE_LEN];

BluetoothReportMode bluetoothReportMode = BT_REPORT_SNIFF;

char bluetoothReportTitle[REPORT_TITLE_LEN];
char bluetoothReportLines[MAX_REPORT_LINES][REPORT_LINE_LEN];
int bluetoothReportLineCount = 0;

// ================= OLED SAFE TEXT =================

bool isAllowedOLEDChar(char c) {
  if (c >= 'A' && c <= 'Z') return true;
  if (c >= 'a' && c <= 'z') return true;
  if (c >= '0' && c <= '9') return true;

  if (c == ' ') return true;
  if (c == '-') return true;
  if (c == ':') return true;
  if (c == '.') return true;
  if (c == '/') return true;
  if (c == '_') return true;

  return false;
}

void copyCleanText(char* dest, const char* src, int maxLen) {
  int i = 0;

  if (dest == NULL || maxLen <= 0) {
    return;
  }

  if (src == NULL) {
    dest[0] = '\0';
    return;
  }

  while (src[i] != '\0' && i < maxLen - 1) {
    char c = src[i];

    if (!isAllowedOLEDChar(c)) {
      c = ' ';
    }

    dest[i] = c;
    i++;
  }

  dest[i] = '\0';
}

char toUpperAscii(char c) {
  if (c >= 'a' && c <= 'z') {
    return c - 32;
  }

  return c;
}

bool stringsEqualIgnoreCase(const char* a, const char* b) {
  if (a == NULL || b == NULL) {
    return false;
  }

  int i = 0;

  while (a[i] != '\0' && b[i] != '\0') {
    if (toUpperAscii(a[i]) != toUpperAscii(b[i])) {
      return false;
    }

    i++;
  }

  return a[i] == '\0' && b[i] == '\0';
}
