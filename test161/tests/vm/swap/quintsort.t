---
name: "Quint Sort (Swap)"
description: >
  Run five concurent copies of sort.
tags: [swap]
depends: [swap-basic, /vm/quintsort.t, shell]
sys161:
  cpus: 2
  ram: 2M
  disk1:
    enabled: true
monitor:
  progresstimeout: 30.0
  commandtimeout: 1100.0
misc:
  prompttimeout: 3600.0
---
khu
$ /testbin/quintsort
khu
