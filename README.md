

## initialize demo

https://forums.raspberrypi.com/viewtopic.php?t=349107


## build

```shell

mkdir build
cd build

cmake -DPICO_BOARD=pico2_w ..

make
```

## 串口输出

```shell
minicom -b 115200 -o -D /dev/tty.usbmodem1101
minicom -b 115200 -o -D /dev/cu.usbmodem1101
```
