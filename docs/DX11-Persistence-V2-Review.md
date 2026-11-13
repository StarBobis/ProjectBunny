# DX11 持久化 V2 适配与验证

对应4bd226f7、13f88a7a、c0675d4b、015d3394、60f2cd44；共享追加工具另对应fa3c42cd。完整SHA在逐提交账本。

生产只读新键 clear_unknown_settings，缺省true，供应模板显式0。旧auto_clear_persist_vars键不提供隐式兼容别名，旧Globals字段及测试保存/恢复引用已迁移。已移除persist的普通变量和池槽不再执行旧user赋值，但有效旧literal可作为unknown保留，恢复persist后能再次识别。LOCKED继续遵守本地不可变/不保存约定。

未知值每次从实际user输入重新收集，按照归属路径、名字和float32位模式组成完整映射。与上次成功处理的映射精确比较，承接上游名字+值CRC意图而不引入碰撞误删；同key改值和正负零会更新宽限期。Constants执行后再确认，使恢复的mod及lazy持久槽能够先被识别。有效unknown不再单纯触发dirtybit2；旧bit2仅用于失败/无效输入等本地重试状态。changed→unchanged赋值不会抹掉已经挂起的dirty。

上游015先rewrite已知再append未知，两次写中间失败可能丢未知。本地一次合并已知+应保留未知，使用同目录CREATE_NEW、既有安全属性的临时文件，检查ferror/fclose后MoveFileEx替换；成功才提交内存快照/清dirty，失败只清理自己创建的临时文件，不预删原目标。shared wfopen_exclusive_ensuring_access只接受w/wb/wt，碰撞EEXIST不截断旧文件。

清理关闭且集合未变、known无dirty时不改文件。独立审查发现非空unknown变空时也必须跳过额外写盘，已加非空条件；仍更新previous/count，不保留陈旧确认。预保存失败发生在ClearKeyBindings/OverrideSave.Reset前，保留旧live registry；Constants后的清理替换失败保留本轮新registry，pending和dirty维持可重试，不声称整个Load已回滚。

新增真实文件/WARP用例执行：新键/缺省/旧键不影响；失persist普通/池的旧值拒应用及重新识别；samekey新值/±0新宽限；普通变量changed→unchanged、NaN/±0脏位；确定旧mtime与binary读取证明未变集合、非空变空均不重写；真实目标file share锁分别令预保存/晚清理替换失败，原文件完整字节不变，旧/新registry归属及pending准确；释放锁后重试成功。

旧unknown-pool回归一项原本假设每次reload都会把用户文件重新格式化成带空格输出。V2实际正确跳过I/O后，count1/dirty0/known_b22正确，但文件保留$unknown_a=11原格式；该断言迁为完整原文件保留、未知数量和实际known值，同时仍验证下一确认清理，不强迫无意义重写。

验证：最终DX11 DLL/test构建成功，完整CTest1/1，test1.57秒/total1.58秒；证据build/upstream/persist-v2-integrated-{build,ctest,LastTest}.log。共享工具引发的DX12兼容重编9/9与链接通过，build/upstream/build-dx12-compat.log。真实安全追加、binary/text、seek后尾写、sharing失败/重试、exclusive碰撞和非法mode无副作用也在同一完整suite通过。未部署、未执行目标游戏或并发deferred config全链验证。
