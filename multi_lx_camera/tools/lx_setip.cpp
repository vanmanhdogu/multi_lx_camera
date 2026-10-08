// Give a camera a new static IP, selecting it by MAC address.
//
// MAC is the only selector that is trustworthy when two cameras share an IP.
// DcOpenDevice's OPEN_BY_SN / OPEN_BY_ID / OPEN_BY_INDEX modes all resolve the
// selector to an IP first and then connect to that IP, so on a conflicted
// network they silently hand you the wrong camera -- asking for the S10's
// serial really does return the M4Mega. This tool therefore refuses to act
// unless the device it actually opened reports the MAC you asked for.
//
// The new address takes effect only after the camera is power-cycled.
//
// Build: see build.sh in this directory.

#include "lx_camera_api.h"
#include "lx_camera_define.h"
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <string>

static std::string BareIp(const char *ip_port) {
  std::string s(ip_port);
  auto colon = s.find(':');
  return colon == std::string::npos ? s : s.substr(0, colon);
}

int main(int argc, char **argv) {
  if (argc < 3) {
    printf("usage: lx_setip <mac> <new-ip> [netmask] [gateway]\n"
           "       netmask defaults to 255.255.255.0\n"
           "       gateway defaults to the new IP with the last octet set to 1\n\n"
           "example: lx_setip 02:3d:f9:61:85:07 192.168.100.83\n");
    return 1;
  }
  const char *want_mac = argv[1];
  const char *new_ip   = argv[2];
  const char *netmask  = (argc >= 4) ? argv[3] : "255.255.255.0";
  const char *gateway  = (argc >= 5) ? argv[4] : nullptr;

  LxDeviceInfo *list = nullptr;
  int n = 0;
  if (DcGetDeviceList(&list, &n, LX_SERIAL_ALL) != LX_SUCCESS || n == 0) {
    printf("no cameras found\n");
    return 2;
  }

  // Find the requested MAC in the discovery list and note its IP -- that IP is
  // all DcOpenDevice can be told, which is why the post-open check below is not
  // optional.
  int idx = -1;
  for (int i = 0; i < n; ++i)
    if (strcasecmp(list[i].mac, want_mac) == 0) { idx = i; break; }
  if (idx < 0) {
    printf("MAC %s is not in the device list. Run lx_discover to see what is.\n", want_mac);
    return 3;
  }
  std::string ip = BareIp(list[idx].ip);
  printf("target: mac=%s currently at %s\n", list[idx].mac, ip.c_str());

  DcHandle h = 0;
  LxDeviceInfo got;
  int ret = DcOpenDevice(OPEN_BY_IP, ip.c_str(), &h, &got);
  if (ret != LX_SUCCESS) {
    printf("open %s failed: %d\n", ip.c_str(), ret);
    return 4;
  }
  printf("opened: mac=%s sn=%s\n", got.mac, got.sn);

  // Refuse to write an IP into a camera we did not mean to touch.
  if (strcasecmp(got.mac, want_mac) != 0) {
    printf("\n!! ABORTING: asked for MAC %s but the network handed us %s.\n"
           "   That is the duplicate-IP symptom. Disconnect one camera from the\n"
           "   hub, re-address the one that is left, then reconnect the other.\n",
           want_mac, got.mac);
    DcCloseDevice(h);
    return 5;
  }

  ret = DcSetCameraIp(h, new_ip, netmask, gateway);
  printf("DcSetCameraIp(%s, %s, %s) -> %d\n", new_ip, netmask,
         gateway ? gateway : "(auto)", ret);
  DcCloseDevice(h);

  if (ret != LX_SUCCESS) {
    printf("FAILED\n");
    return 6;
  }
  printf("\nOK. Power-cycle this camera, then run lx_discover to confirm it\n"
         "came back at %s.\n", new_ip);
  return 0;
}
