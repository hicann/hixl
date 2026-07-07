## 环境类建议
> 如果现象符合环境问题，可建议用户做对应的排查动作。

### A2/A3
#### ROCE 连通性

- 环境 ROCE 没有配置连通，会导致建链失败。排除其他明显原因后，主动检查 device IP 和 device 间联通性。

```bash
# 接收端
/usr/local/Ascend/driver/tools/hccn_tool -i 0 -roce_test reset
/usr/local/Ascend/driver/tools/hccn_tool -i 0 -roce_test ib_send_bw -s 65536 -n 1000 -tcp

# 发送端
/usr/local/Ascend/driver/tools/hccn_tool -i 1 -roce_test reset
PEER_IP=$(/usr/local/Ascend/driver/tools/hccn_tool -i 0 -ip -g 2>/dev/null | sed -n 's/^ipaddr:\(.*\)/\1/p' | head -1)
/usr/local/Ascend/driver/tools/hccn_tool -i 1 -roce_test ib_send_bw -s 65536 -n 1000 address "$PEER_IP" -tcp
```

#### 网卡状态

- 网卡处于 `DOWN` 时，会导致建链失败或传输失败；排除其他明显问题后要主动检查链路状态。
- 查询当前网卡状态：

```bash
for i in {0..15}; do /usr/local/Ascend/driver/tools/hccn_tool -i $i -link -g; done
```

- 如果当前是 `UP`，但历史上曾在传输时刻 `DOWN`，同样可能是根因；继续查历史状态：

```bash
for i in {0..15}; do /usr/local/Ascend/driver/tools/hccn_tool -i $i -link_stat -g; done
```

#### FabricMem模式内存申请

使用FabricMem模式时，如果问题是申请host内存报 out of memory。
可以调用 `python3 scripts/numa_intersect.py` 来检测有多少 HOST 内存可申请。
