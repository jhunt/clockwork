#!/usr/bin/perl
use strict;
use warnings;

use Test::More;
use t::common;

gencode_ok "use host service1.test", <<'EOF', "service resource";
RESET
;; res_service snmpd
SET %A "cwtool svc-boot-status snmpd"
CALL &EXEC.CHECK
OK? @enabled.1
  SET %A "cwtool svc-enable snmpd"
  CALL &EXEC.CHECK
enabled.1:
SET %A "cwtool svc-run-status snmpd"
CALL &EXEC.CHECK
OK? @running.1
  SET %A "cwtool svc-init snmpd start"
  CALL &EXEC.CHECK
  FLAG 1 :changed
  FLAG 0 :res0
running.1:
!FLAGGED? :res0 @next.1
  SET %A "cwtool svc-init snmpd restart"
  CALL &EXEC.CHECK
  FLAG 0 :res0
next.1:
!FLAGGED? :changed @final.1
final.1:
EOF

gencode_ok "use host service2.test", <<'EOF', "stopped service";
RESET
;; res_service microcode
SET %A "cwtool svc-run-status microcode"
CALL &EXEC.CHECK
NOTOK? @stopped.1
  SET %A "cwtool svc-init microcode stop"
  CALL &EXEC.CHECK
  FLAG 1 :changed
  FLAG 0 :res0
stopped.1:
!FLAGGED? :res0 @next.1
  SET %A "cwtool svc-init microcode stop"
  CALL &EXEC.CHECK
  FLAG 0 :res0
next.1:
!FLAGGED? :changed @final.1
final.1:
EOF

gencode_ok "use host service3.test", <<'EOF', "service resource";
RESET
;; res_service neverwhere
SET %A "cwtool svc-boot-status neverwhere"
CALL &EXEC.CHECK
NOTOK? @disabled.1
  SET %A "cwtool svc-disable neverwhere"
  CALL &EXEC.CHECK
disabled.1:
SET %A "cwtool svc-run-status neverwhere"
CALL &EXEC.CHECK
NOTOK? @stopped.1
  SET %A "cwtool svc-init neverwhere stop"
  CALL &EXEC.CHECK
  FLAG 1 :changed
  FLAG 0 :res0
stopped.1:
!FLAGGED? :res0 @next.1
  SET %A "cwtool svc-init neverwhere stop"
  CALL &EXEC.CHECK
  FLAG 0 :res0
next.1:
!FLAGGED? :changed @final.1
final.1:
EOF


done_testing;
