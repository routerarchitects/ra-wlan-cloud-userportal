
none
psk             == wpa1 - personal
psk2            == wpa2 - personal
sae             == wpa3 - personal
sae-mixed       == wpa2/3 - personal
psk-mixed       == wpa1/2 - personal
wpa             == wpa1 - enterprise
wpa2            == wpa2 - enterprise
wpa-mixed       == wpa1/2 - enterprise
wpa3            == wpa3 - enterprise
wpa3-192        == wpa3 transition mode - enterprise (weird mode ignore it for now)
wpa3-mixed      == wpa2/3 - enterprise
8:39
we do not support WEP
8:39
and we do nto support TKIP
8:39
both are considered broken
8:40
and ignore wpa3-192 …. its some werid mode



https://git.openwrt.org/?p=openwrt/openwrt.git;a=blob;f=package/network/services/hos[…]hostapd.sh;h=d9d5f348775debade847f267b1ca1dc86444e41d;hb=HEAD
9:16
ok, so you can set 11w to any of the 3 values but optional should be default
9:16
BUT
9:16
if you do raw wpa3 personal/enterprise it needs to be required
9:17
and if you do any wpa3 mixed ode with wpa2 it needs to be optional or required









