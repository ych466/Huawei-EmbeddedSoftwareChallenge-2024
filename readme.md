# 2024华为嵌入式软件大赛亚军

#### 队名

尊嘟假嘟

#### GitHub

https://github.com/ChenYueDong-china/Huawei-EmbeddedSoftwareChallenge-2024.git

#### 题目内容

本题目需要参赛者设计一种高效的算法，通过智能地规划和调整网络中的业务流，以应对网络中可能发生的多重断纤事件。

具体就是网络会断边，然后需要重新规划路径，复赛增加了自己生成断边序列，决赛增加了自己调整节点变通道能力。

#### 方案总结

寻路策略：

- 把节点变通道能力当作一种资源，边上的通道也当作一种资源，分别给予权重，作为参数微调
- 预测未来会影响的业务总价值，具体做法是预测剩余的断边数（通过统计每个场景的最大断边数），乘以断一条边平均影响的业务价值（所有边上的总价值除以总边数）
- 按当前影响的业务价值/未来会影响的业务总价值分配剩余的空闲资源，作为业务能占据的额外资源
- 业务至多占据的资源为老路径占据的资源加分配给他的额外资源
- 按业务价值排序，使用A*算法寻路，一旦寻路超过最大资源则跳出，认为寻不到路径
- 如果时间有剩余，随机对业务排序，重新寻路，如果能拯救的业务价值更高，则使用这个解

生成断边策略：

- 主要分为生成段和替换段，生成段先贪心随机生成30个候选序列，替换段不断去贪心随机生成一个序列尝试去替换分差最小的序列保存
- 生成一个序列：使用贪心的方式随机生成候选断边序列，模拟断边，对每一个序列选出分差最大的长度截断，选择最大分差的序列保存
- 贪心随机：对边打分，选择分差最大前20条边随机选择一条边去断掉，然后更新某些边的得分，不断重复直到达到需要断边的长度
- 对边打分：模拟断一条边，baseline能拯救的价值和自己的A*能拯救的价值，两个价值相减作为基础边的分数。
- 更新得分：选择某一条边之后，我们需要更新与这个边相关的其他的边的得分，具体做法是我们保存边上的业务，对该业务的原始路径上的所有未选择的边降低业务的价值的分数。后期也增加了点东西，效果不一定好，具体做法是该业务baseline能寻到的路径上的边加业务价值的分数（让他下次死亡机会大一些），baseline不能寻到的路径上的边减去业务价值的分数（已经死亡，不需要再次死亡），自己的A*能寻到路径上的边减去业务价值的分数（让他下次死亡机会降低）

调整变通道能力策略：

- 使用按节点权重分配策略，有多余的次数则按权重次序一个个分完

- 节点权重：使用baseline去寻一条最短路，对最短路经过的每一个中间节点（不包含首尾节点）增加1的权重

#### 注意

版权属于华为，侵权删除