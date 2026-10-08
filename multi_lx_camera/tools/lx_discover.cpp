// Enumerate every MRDVS camera the SDK can see on the network.
//
// The ROS driver has no way to show you this: lx_camera_node calls
// DcGetDeviceList internally and immediately opens one device, so a duplicate
// IP or a missing camera only ever surfaces as "Open device failed!". This
// tool prints the raw device list, which is what you need to diagnose both.
//
// Discovery is a UDP broadcast, so it finds cameras even when their IP is on a
// different subnet than the host -- and it lists two cameras that share one IP
// as two separate entries, which is exactly the case this tool exists to catch.
//
// Build: see build.sh in this directory.

#include "lx_camera_api.h"
#include "lx_camera_define.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static const char *TypeName(int t) {
  switch (t) {
  case LX_DEVICE_M2:        return "M2";
  case LX_DEVICE_M3:        return "M3";
  case LX_DEVICE_M4Pro:     return "M4Pro";
  case LX_DEVICE_M4_MEGA:   return "M4Mega";
  case LX_DEVICE_M4:        return "M4";
  case LX_DEVICE_M4V1_1:    return "M4V1.1";
  case LX_DEVICE_M4ProV1_1: return "M4ProV1.1";
  case LX_DEVICE_S1:        return "S1";
  case LX_DEVICE_S2:        return "S2";
  case LX_DEVICE_S3:        return "S3";
  case LX_DEVICE_S10:       return "S10";
  case LX_DEVICE_S10PRO:    return "S10Pro";
  case LX_DEVICE_S10ULTRA:  return "S10Ultra";
  case LX_DEVICE_S11:       return "S11";
  default:                  return "unknown";
  }
}

// "192.168.100.82:3956" -> "192.168.100.82". Device IPs carry the GigE control
// port, but the ROS driver's `ip` parameter wants the bare address.
static std::string BareIp(const char *ip_port) {
  std::string s(ip_port);
  auto colon = s.find(':');
  return colon == std::string::npos ? s : s.substr(0, colon);
}

int main() {
  LxDeviceInfo *list = nullptr;
  int n = 0;
  int ret = DcGetDeviceList(&list, &n, LX_SERIAL_ALL);
  if (ret != LX_SUCCESS) {
    printf("DcGetDeviceList failed: %d\n", ret);
    return 1;
  }
  printf("\n==== %d MRDVS device(s) found ====\n", n);

  std::map<std::string, std::vector<int>> by_ip;
  for (int i = 0; i < n; ++i) {
    by_ip[BareIp(list[i].ip)].push_back(i);
    printf("\n[index %d] %s\n", i, TypeName((int)list[i].dev_type));
    printf("  ip        : %s   (driver param: ip:=\"%s\")\n",
           list[i].ip, BareIp(list[i].ip).c_str());
    printf("  mac       : %s\n", list[i].mac);
    printf("  sn        : %s\n", list[i].sn);
    printf("  id        : %s\n", list[i].id);
    printf("  firmware  : %s\n", list[i].firmware_ver);
    printf("  netmask   : %s\n", list[i].reserve);
    printf("  gateway   : %s\n", list[i].reserve2);
    printf("  name      : %s\n", list[i].name);
  }

  // The whole point of the tool. Two cameras on one IP cannot both be opened:
  // the SDK resolves sn/id/index to an IP before it connects, so every open
  // lands on whichever device currently wins the ARP race, and the second open
  // returns LX_E_CTRL_PERMISS_ERROR (-9).
  int clashes = 0;
  for (const auto &kv : by_ip) {
    if (kv.second.size() < 2) continue;
    ++clashes;
    printf("\n!! IP CONFLICT: %s is claimed by %zu cameras:",
           kv.first.c_str(), kv.second.size());
    for (int i : kv.second) printf(" %s(%s)", TypeName((int)list[i].dev_type), list[i].mac);
    printf("\n   Only one of them can be opened. Re-address one with lx_setip\n"
           "   before launching any driver. See lx_camera_ros/docs/multi_lidar.md §3.\n");
  }
  if (!clashes && n > 1)
    printf("\nAll %d cameras have distinct IPs -- ready for multi-camera launch.\n", n);
  printf("\n");
  return clashes ? 2 : 0;
}
