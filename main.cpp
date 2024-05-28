/*
 * Description: SimpleDemo，主要思路是不断随机迭代搜索+选择性拯救业务获得最优解
 * Date: 2024-05-18
 */
#include <iostream>
#include <string>
#include <random>
#include <chrono>
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include <cassert>
#include <algorithm>
#include <map>
#include <stack>
#include <cstring>

using namespace std;

//迭代参数
const int RANDOM_SEED = 666;//种子
const int MIN_ITERATION_COUNT = 1;//最低穷举次数
const bool IS_ONLINE = false;//是否使劲穷举

//业务是否不救参数
const int REMAIN_COUNT_RECOVERY = 5;//剩余几个一定救，只有t大于1有用，猜测线上每一次t的断边个数都一样
const double SHOULD_DIE_FACTOR = 5.0;//不救哪些业务因子
const double SHOULD_DIE_MIN_RECOVER_RATE = 0.8;//救活率低于这个才不救

//常量
static int MAX_E_FAIL_COUNT = 6000;
const int SEARCH_TIME = 90 * 1000;
const int MAX_M = 1000;
const int MAX_N = 200;
const int CHANNEL_COUNT = 40;
const auto programStartTime = std::chrono::steady_clock::now();
const int INT_INF = 0x7f7f7f7f;

inline unsigned long long runtime() {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - programStartTime);
    return duration.count();
}

inline void printError(const string &s) {
    fprintf(stderr, "%s\n", s.c_str());
    fflush(stderr);
}

//边
struct Edge {
    int from{};
    int to{};
    bool die{};
    int weight{};
    int channel[CHANNEL_COUNT + 1]{};//0号不用
    int freeChannelTable[CHANNEL_COUNT + 1][CHANNEL_COUNT + 1]{};//打表加快搜索速度
    Edge() {
        memset(channel, -1, sizeof(channel));
        weight = 1;
    }

    void reset() {
        memset(channel, -1, sizeof(channel));
        die = false;//没有断掉
        weight = 1;
    }
};

//邻接表的边
struct NearEdge {
    int id;
    int to;
};

//业务
struct Business {
    int id{};
    int from{};
    int to{};
    int needChannelLength{};
    int value{};
    bool die{};

    bool operator<(const Business &other) const {
        return value > other.value;
    }

    void reset() {
        die = false;
    }

};

//顶点
struct Vertex {
    int maxChangeCount{};
    int curChangeCount{};
    unordered_set<int> changeBusIds;
    bool die{};//假设死亡不会变业务,有变通道次数且不die

    void reset() {
        curChangeCount = maxChangeCount;
        changeBusIds.clear();
        die = false;
    }
};

//策略类
struct Strategy {
    int N{};//节点数
    int M{};//边数
    default_random_engine rad{RANDOM_SEED};
    vector<Edge> edges;
    vector<Vertex> vertices;
    vector<vector<NearEdge>> graph;//邻接表
    vector<NearEdge> searchGraph[MAX_N + 1];//邻接表
    vector<Business> buses;//邻接表
    struct Point {
        int edgeId;
        int startChannelId;
        int endChannelId;
    };
    vector<vector<Point>> busesOriginResult;//邻接表
    int minDistance[MAX_N + 1][MAX_N + 1]{};//邻接表
    unsigned long long searchTime = 0;
    int curHandleCount = 0;
    double maxScore{};
    double curScore{};
    int recoveryCount = 0;
    int maxDieCount = 0;

    struct SearchUtils {
        inline static vector<Point> aStar(int start, int end, int width, const vector<NearEdge> searchGraph[MAX_N + 1],
                                          const vector<Edge> &edges, const vector<Vertex> &vertices,
                                          int minDistance[MAX_N + 1][MAX_N + 1]) {
            static int dist[CHANNEL_COUNT + 1][MAX_N + 1]
            , parentEdgeId[CHANNEL_COUNT + 1][MAX_N + 1]
            , parentStartChannel[CHANNEL_COUNT + 1][MAX_N + 1]
            , parentVertexId[CHANNEL_COUNT + 1][MAX_N + 1]
            , timestamp[CHANNEL_COUNT + 1][MAX_N + 1]
            , timestampId = 1;//距离
            static bool conflictPoints[MAX_M + 1][MAX_N + 1];
            timestampId++;
            struct PointWithChannelDeep {
                int channelVertex;//前十六位是channel，后十六位是vertexId
                int deep;

                PointWithChannelDeep(int channelVertex, int deep) : channelVertex(channelVertex), deep(deep) {}
            };
            static map<int, stack<PointWithChannelDeep>> cacheMap;
            cacheMap.clear();
            stack<PointWithChannelDeep> &tmp = cacheMap[minDistance[start][end] * MAX_M];
            for (int i = 1; i <= CHANNEL_COUNT; ++i) {
                dist[i][start] = 0;
                timestamp[i][start] = timestampId;
                parentVertexId[i][start] = -1;//起始点为-1顶点
                tmp.emplace((i << 16) + start, 0);
            }
            int endChannel = -1;
            int count = 0;

            while (!cacheMap.empty()) {
                pair<const int, stack<PointWithChannelDeep>> &entry = *cacheMap.begin();
                const int totalDeep = entry.first;
                stack<PointWithChannelDeep> &sk = entry.second;
                const PointWithChannelDeep poll = sk.top();
                sk.pop();
                const int lastChannel = poll.channelVertex >> 16;
                const int lastVertex = poll.channelVertex & 0xFFFF;
                const int lastDeep = poll.deep;
                if (lastDeep != dist[lastChannel][lastVertex]) {
                    if (sk.empty()) {
                        cacheMap.erase(totalDeep);
                    }
                    continue;
                }
                if (lastVertex == end) {
                    endChannel = lastChannel;
                    break;
                }
                for (const NearEdge &nearEdge: searchGraph[lastVertex]) {
                    const int next = nearEdge.to;
                    if (parentVertexId[lastChannel][lastVertex] == next) {
                        //防止重边
                        continue;
                    }
                    const Edge &edge = edges[nearEdge.id];
                    if (edge.die) {
                        continue;
                    }
                    if (conflictPoints[nearEdge.id][next]) {
                        continue;//顶点重复
                    }
                    const int *freeChannelTable = edge.freeChannelTable[width];
                    for (int i = 1; i <= freeChannelTable[0]; ++i) {
                        count++;
                        const int startChannel = freeChannelTable[i];
                        //用来穷举
                        int nextDistance = lastDeep + edge.weight * MAX_M;
                        if (startChannel != lastChannel) {
                            if (lastVertex == start || vertices[lastVertex].curChangeCount <= 0) {
                                //开始节点一定不需要转换,而且也转换不了
                                continue;
                            }
                            if (vertices[lastVertex].die) {
                                continue;//todo 预测，die之后没法转换,die之后curchangeCount可以直接为0
                            }
                            nextDistance += 1;
                        }

                        if (timestamp[startChannel][next] == timestampId &&
                            dist[startChannel][next] <= nextDistance) {
                            //访问过了，且距离没变得更近
                            continue;
                        }
                        timestamp[startChannel][next] = timestampId;
                        dist[startChannel][next] = nextDistance;
                        parentStartChannel[startChannel][next] = lastChannel;
                        parentEdgeId[startChannel][next] = nearEdge.id;
                        parentVertexId[startChannel][next] = lastVertex;
                        cacheMap[nextDistance + MAX_M * minDistance[next][end]]
                                .emplace((startChannel << 16) + next, nextDistance);
                    }
                }
                if (sk.empty()) {
                    cacheMap.erase(totalDeep);
                }
            }
            if (endChannel == -1) {
                return {};
            }
            vector<Point> path;
            int cur = end;
            int curStartChannel = endChannel;
            while (cur != start) {
                int edgeId = parentEdgeId[curStartChannel][cur];
                path.push_back({edgeId, curStartChannel, curStartChannel + width - 1});
                int startChannel = parentStartChannel[curStartChannel][cur];
                cur = parentVertexId[curStartChannel][cur];
                curStartChannel = startChannel;
            }
            reverse(path.begin(), path.end());
            int from = start;
            unordered_set<int> keys;
            keys.insert(from);
            for (const Point &point: path) {
                const Edge &edge = edges[point.edgeId];
                int to = edge.from == from ? edge.to : edge.from;
                if (keys.count(to)) {
                    //顶点重复，需要锁死这个通道进入得顶点
                    // return  {};
                    int tmpFrom = start;
                    int lockE = -1;
                    int lockV = -1;
                    for (const Point &point1: path) {
                        const Edge &edge1 = edges[point1.edgeId];
                        if (tmpFrom == to) {
//                                lockC = point1.startChannelId;
                            lockE = point1.edgeId;
                            lockV = edge1.from == tmpFrom ? edge1.to : edge1.from;
                            break;
                        }
                        tmpFrom = edge1.from == tmpFrom ? edge1.to : edge1.from;
                    }

                    conflictPoints[lockE][lockV] = true;
                    path = aStar(start, end, width, searchGraph, edges, vertices, minDistance);
                    conflictPoints[lockE][lockV] = false;
                    return path;
                }
                keys.insert(to);
                from = to;
            }
            return path;
        }
    };

    //恢复场景
    void reset() {
        //恢复边和顶点状态
        for (Edge &edge: edges) {
            edge.reset();
        }
        for (Vertex &vertex: vertices) {
            vertex.reset();
        }
        for (Business &bus: buses) {
            bus.reset();
        }
        for (int i = 1; i < busesOriginResult.size(); i++) {
            vector<Point> &result = busesOriginResult[i];
            for (const Point &point: result) {
                //只需要占用通道，顶点变换能力不需要，最初没有
                for (int j = point.startChannelId; j <= point.endChannelId; j++) {
                    edges[point.edgeId].channel[j] = i;
                }
            }
        }
        for (int i = 1; i <= N; ++i) {
            searchGraph[i] = graph[i];
        }
        for (int i = 1; i < edges.size(); i++) {
            Edge &edge = edges[i];
            //计算表
            updateEdgeChannelTable(edge);
        }
    }

    //获得路径经过的顶点
    inline unordered_set<int> getOriginChangeV(const Business &business, const vector<Point> &path) {
        unordered_set<int> originChangeV;
        if (!path.empty()) {
            int from = business.from;
            int lastChannel = path[0].startChannelId;
            for (const Point &point: path) {
                int to = edges[point.edgeId].from == from ?
                         edges[point.edgeId].to
                                                          : edges[point.edgeId].from;
                if (point.startChannelId != lastChannel) {
                    originChangeV.insert(from);
                }
                from = to;
                lastChannel = point.startChannelId;
            }
        }
        return originChangeV;
    }

    //获得路径经过的边
    inline static unordered_map<int, int> getOriginEdgeIds(const vector<Point> &path) {
        unordered_map<int, int> result;
        if (!path.empty()) {
            for (const Point &point: path) {
                result[point.edgeId] = point.startChannelId;
            }
        }
        return result;
    }

    //更新边的通道表，加快aStar搜索速度使用
    inline static void updateEdgeChannelTable(Edge &edge) {
        const int *channel = edge.channel;
        int (*freeChannelTable)[CHANNEL_COUNT + 1] = edge.freeChannelTable;
        int freeLength = 0;
        for (int i = 1; i <= CHANNEL_COUNT; i++) {
            freeChannelTable[i][0] = 0;//长度重新置为0
        }
        for (int i = 1; i <= CHANNEL_COUNT; i++) {
            if (channel[i] == -1) {
                if (channel[i] == channel[i - 1]) {
                    freeLength++;
                } else {
                    freeLength = 1;
                }
            } else {
                freeLength = 0;
            }
            if (freeLength > 0) {
                for (int j = freeLength; j > 0; j--) {
                    int start = i - j + 1;
                    freeChannelTable[j][++freeChannelTable[j][0]] = start;
                }
            }
        }
    }

    //在老路径基础上，回收新路径
    inline void undoBusiness(const Business &business, const vector<Point> &newPath, const vector<Point> &originPath) {
        //变通道次数加回来
        unordered_set<int> originChangeV = getOriginChangeV(business, originPath);
        unordered_map<int, int> originEdgeIds = getOriginEdgeIds(originPath);
        int from = business.from;
        int lastChannel = newPath[0].startChannelId;
        for (const Point &point: newPath) {
            Edge &edge = edges[point.edgeId];
            assert(edge.channel[point.startChannelId] == business.id);
            for (int j = point.startChannelId; j <= point.endChannelId; j++) {
                if (originEdgeIds.count(point.edgeId)
                    && j >= originEdgeIds[point.edgeId]
                    && j <= originEdgeIds[point.edgeId]
                            + business.needChannelLength - 1) {
                    continue;
                }
                edge.channel[j] = -1;
            }
            updateEdgeChannelTable(edge);
            int to = edge.from == from ? edge.to : edge.from;
            if (point.startChannelId != lastChannel && !originChangeV.count(from)) {
                vertices[from].curChangeCount++;
                vertices[from].changeBusIds.erase(business.id);//没改变
                assert(vertices[from].curChangeCount <= vertices[from].maxChangeCount);
            }
            from = to;
            lastChannel = point.startChannelId;
        }
    }

    //在老路径基础上，增加新路径
    inline void redoBusiness(const Business &business, const vector<Point> &newPath, const vector<Point> &originPath) {
        unordered_set<int> originChangeV = getOriginChangeV(business, originPath);
        int from = business.from;
        int lastChannel = newPath[0].startChannelId;
        for (const Point &point: newPath) {
            Edge &edge = edges[point.edgeId];
            //占用通道
            for (int j = point.startChannelId; j <= point.endChannelId; j++) {
                assert(edge.channel[j] == -1 || edge.channel[j] == business.id);
                edge.channel[j] = business.id;
            }
            updateEdgeChannelTable(edge);
            int to = edge.from == from ? edge.to : edge.from;
            if (point.startChannelId != lastChannel
                && !originChangeV.count(from)) {//包含可以复用资源
                //变通道，需要减
                vertices[from].curChangeCount--;
                vertices[from].changeBusIds.insert(business.id);
            }
            from = to;
            lastChannel = point.startChannelId;
        }
    }

    //aStar寻路
    vector<Point> aStarFindPath(Business &business, vector<Point> &originPath) {
        int from = business.from;
        int to = business.to;
        int width = business.needChannelLength;
        unsigned long long int l1 = runtime();
        undoBusiness(business, originPath, {});
        vector<Point> path = SearchUtils::aStar(from, to, width,
                                                searchGraph, edges, vertices, minDistance);
        redoBusiness(business, originPath, {});
        unsigned long long int r1 = runtime();
        searchTime += r1 - l1;
        return path;
    }

    //把全部增加上的新路径回收掉
    void undoResult(const unordered_map<int, vector<Point>> &result, const vector<vector<Point>> &curBusesResult) {
        for (const auto &entry: result) {
            int id = entry.first;
            const vector<Point> &newPath = entry.second;
            Business &business = buses[id];
            undoBusiness(business, newPath, curBusesResult[business.id]);
        }
    }

    //把全部需要增加上的新路径增加进去，并且回收老路径
    void redoResult(vector<int> &affectBusinesses, unordered_map<int, vector<Point>> &result,
                    vector<vector<Point>> &curBusesResult) {
        for (const auto &entry: result) {
            int id = entry.first;
            const vector<Point> &newPath = entry.second;
            const vector<Point> &originPath = curBusesResult[id];
            const Business &business = buses[id];
            //先加入新路径
            redoBusiness(business, newPath, originPath);
            //未错误，误报
            undoBusiness(business, originPath, newPath);
            curBusesResult[business.id] = newPath;
        }
        for (const int &id: affectBusinesses) {
            Business &business = buses[id];
            if (!result.count(business.id)) {
                business.die = true;//死掉了，以后不调度
            }
        }
    }

    //输出结果
    inline void printResult(const unordered_map<int, vector<Point>> &resultMap) {
        printf("%d\n", int(resultMap.size()));
        for (auto &entry: resultMap) {
            int id = entry.first;
            const vector<Point> &newPath = entry.second;
            Business &business = buses[id];
            printf("%d %d\n", business.id, int(newPath.size()));
            for (int j = 0; j < newPath.size(); j++) {
                const Point &point = newPath[j];
                printf("%d %d %d", point.edgeId, point.startChannelId, point.endChannelId);
                if (j < int(newPath.size()) - 1) {
                    printf(" ");
                } else {
                    printf("\n");
                }
            }
        }
        fflush(stdout);
    }

    //给路径打分，随机穷举使用
    double getEstimateScore(const unordered_map<int, vector<Point>> &satisfyBusesResult) {
        int totalValue = 0;
        for (const auto &entry: satisfyBusesResult) {
            totalValue += buses[entry.first].value;
        }
        //原则，value，width大的尽量短一点
        double plus = 0;
        for (const auto &entry: satisfyBusesResult) {
            int id = entry.first;
            const vector<Point> &newPath = entry.second;
            const Business &business = buses[id];
            int size = int(newPath.size());
            int width = business.needChannelLength;
            int value = business.value;//价值高，size小好，width暂时不用吧
            plus += 1.0 * value / size;
        }
        return totalValue * 1e9 + plus + 1;
    }

    //简单获得一个基础分
    unordered_map<int, vector<Point>>
    getBaseLineResult(const vector<int> &affectBusinesses, vector<vector<Point>> &curBusesResult) {
        unordered_map<int, vector<Point>> satisfyBusesResult;
        for (int id: affectBusinesses) {
            Business &business = buses[id];
            vector<Point> path = aStarFindPath(business, curBusesResult[business.id]);
            if (!path.empty()) {
                //变通道次数得减回去
                redoBusiness(business, path, curBusesResult[business.id]);
                satisfyBusesResult[business.id] = std::move(path);
            }
        }
        return satisfyBusesResult;
    }

    //核心调度函数
    void
    dispatch(bool deleteLongPathBus, double avgBusEveryCValue, int failEdgeId, vector<vector<Point>> &curBusesResult) {
        assert(failEdgeId != 0);
        curHandleCount++;
        edges[failEdgeId].die = true;

        //1.求受影响的业务
        vector<int> affectBusinesses;
        for (int busId: edges[failEdgeId].channel) {
            if (busId != -1 && !buses[busId].die) {
                assert(buses[busId].id == busId);
                if (!affectBusinesses.empty() && affectBusinesses[affectBusinesses.size() - 1]
                                                 == busId) {
                    continue;
                }
                affectBusinesses.push_back(busId);
            }
        }
        sort(affectBusinesses.begin(), affectBusinesses.end(), [&](int aId, int bId) {
            return buses[aId] < buses[bId];
        });
        int affectSize = int(affectBusinesses.size());


        //2.去掉不可能寻到路径的业务，但是因为有重边重顶点，不一定准确，大概准确
        vector<int> tmp;//还是稍微有点用，减少个数
        for (int id: affectBusinesses) {
            Business &business = buses[id];
            vector<Point> path = aStarFindPath(business, curBusesResult[business.id]);
            if (!path.empty()) {
                tmp.push_back(business.id);
            } else {
                business.die = true;//死了没得救
            }
        }
        affectBusinesses = std::move(tmp);


        //3.循环调度,求出最优解保存
        unordered_map<int, vector<Point>> bestResult;
        double bestScore = -1;
        int remainTime = (int) (SEARCH_TIME - 1000 - runtime());//留1s阈值
        int remainMaxCount = max(1, MAX_E_FAIL_COUNT - curHandleCount + 1);
        int maxRunTime = remainTime / remainMaxCount;
        unsigned long long startTime = runtime();
        int iteration = 0;
        while ((((runtime() - startTime) < maxRunTime) && IS_ONLINE)
               || (!IS_ONLINE && iteration < MIN_ITERATION_COUNT)
                ) {
            for (int j = 1; j <= N; j++) {
                shuffle(searchGraph[j].begin(), searchGraph[j].end(), rad);
            }
            unordered_map<int, vector<Point>> satisfyBusesResult = getBaseLineResult(affectBusinesses,
                                                                                     curBusesResult);

            // 考虑死掉一些业务,腾出空间给新的断边寻路?
            unordered_set<int> shouldDieIds;
            for (const auto &entry: satisfyBusesResult) {
                int id = entry.first;
                const vector<Point> &newPath = entry.second;
                Business &business = buses[id];
                const vector<Point> &originPath = curBusesResult[business.id];
                if (originPath.size() >= newPath.size()) {
                    continue;
                }
                double curEveryCValue = 1.0 * business.value /
                                        (business.needChannelLength
                                         * int(newPath.size() - originPath.size()));
                if (curEveryCValue < SHOULD_DIE_FACTOR * avgBusEveryCValue
                    && 1.0 * max(1, recoveryCount) / max(1, maxDieCount)
                       < SHOULD_DIE_MIN_RECOVER_RATE
                    && deleteLongPathBus) {
                    //救活率不高的情况下，选择放弃一些低价值货物
                    undoBusiness(business, newPath, originPath);
                    shouldDieIds.insert(id);
                }
            }
            for (const auto &id: shouldDieIds) {
                satisfyBusesResult.erase(id);
            }

            //2.算分
            double curScore_ = getEstimateScore(satisfyBusesResult);
            if (curScore_ > bestScore) {
                //打分
                bestScore = curScore_;
                bestResult = satisfyBusesResult;
            }

            //3.重排,穷举
            shuffle(affectBusinesses.begin(), affectBusinesses.end(), rad);
            undoResult(satisfyBusesResult, curBusesResult);//回收结果，下次迭代
            iteration++;
        }
        recoveryCount += int(bestResult.size());
        maxDieCount += affectSize;
        redoResult(affectBusinesses, bestResult, curBusesResult);
        printResult(bestResult);
    }

    //初始化
    void init() {
        scanf("%d %d", &N, &M);
        edges.resize(M + 1);
        vertices.resize(N + 1);
        for (int i = 1; i <= N; i++) {
            int maxChangeCount;
            scanf("%d", &maxChangeCount);
            vertices[i].curChangeCount = maxChangeCount;
            vertices[i].maxChangeCount = maxChangeCount;
        }
        graph.resize(N + 1);
        for (int i = 1; i <= M; i++) {
            int ui, vi;
            scanf("%d %d", &ui, &vi);
            edges[i].from = ui;
            edges[i].to = vi;
            graph[ui].push_back({i, vi});
            graph[vi].push_back({i, ui});
        }


        int J;
        scanf("%d", &J);
        buses.resize(J + 1);
        busesOriginResult.resize(J + 1);
        for (int i = 1; i <= J; i++) {
            int Src, Snk, S, L, R, V;
            scanf("%d %d %d %d %d %d", &Src, &Snk, &S, &L, &R, &V);
            buses[i].id = i;
            buses[i].from = Src;
            buses[i].to = Snk;
            buses[i].needChannelLength = R - L + 1;
            buses[i].value = V;
            for (int j = 0; j < S; j++) {
                int edgeId;
                scanf("%d", &edgeId);
                for (int k = L; k <= R; k++) {
                    edges[edgeId].channel[k] = i;
                }
                busesOriginResult[i].push_back({edgeId, L, R});
            }
        }


        queue<int> q;
        for (int i = 1; i <= N; ++i) {
            searchGraph[i] = graph[i];
        }
        for (int start = 1; start <= N; ++start) {
            for (int i = 1; i <= N; ++i) {
                minDistance[start][i] = INT_INF;
            }
            minDistance[start][start] = 0;
            q.push(start);
            int deep = 0;
            while (!q.empty()) {
                int size = int(q.size());
                deep++;
                for (int i = 0; i < size; i++) {
                    int vId = q.front();
                    q.pop();
                    for (const NearEdge &edge: searchGraph[vId]) {
                        if (minDistance[start][edge.to] == INT_INF) {
                            minDistance[start][edge.to] = deep;
                            q.push(edge.to);
                        }
                    }
                }
            }
        }
        for (int i = 1; i < edges.size(); i++) {
            Edge &edge = edges[i];
            //计算表
            updateEdgeChannelTable(edge);
        }
    }

    //主循环
    void mainLoop() {
        int t;
        scanf("%d", &t);
        maxScore = 10000.0 * t;
        double avgBusEveryCValue = 0;
        for (int i = 1; i <= N; i++) {
            avgBusEveryCValue += buses[i].value;
        }
        avgBusEveryCValue = avgBusEveryCValue / (M * CHANNEL_COUNT);
        int maxLength = INT_INF;//每一个测试场景的断边数，默认是无穷个，猜测每次都一样
        for (int i = 0; i < t; i++) {
            //邻接表
            vector<vector<Point>> curBusesResult = busesOriginResult;
            int curLength = 0;
            while (true) {
                int failEdgeId = -1;
                scanf("%d", &failEdgeId);
                if (failEdgeId == -1) {
                    break;
                }
                curLength++;
                dispatch(curLength < maxLength - REMAIN_COUNT_RECOVERY, avgBusEveryCValue, failEdgeId, curBusesResult);
            }
            maxLength = curLength;
            int totalValue = 0;
            int remainValue = 0;
            for (int j = 1; j < buses.size(); j++) {
                totalValue += buses[j].value;
                if (!buses[j].die) {
                    remainValue += buses[j].value;
                }
            }
            curScore += 10000.0 * remainValue / totalValue;
            reset();
        }
    }
};

int main() {
    //    SetConsoleOutputCP ( CP_UTF8 ) ;
//todo 1.估分问题，2.能否不要暴力穷举，加一个不暴力的方式3.重复边或者顶点问题未解决好，4.预测复赛改动点（节点失效or其他？）
    static Strategy strategy;
    if (fopen("../data/0/in.txt", "r") != nullptr) {
        string path = "../data/";
        MAX_E_FAIL_COUNT = 12000;
        long long allCost = 0;
        for (int i = 0; i < 1; i++) {
            unsigned long long start = runtime();
            freopen((path + to_string(i) + "/in.txt").c_str(), "r", stdin);
            freopen((path + to_string(i) + "/out.txt").c_str(), "w", stdout);
            strategy = {};
            strategy.init();
            strategy.mainLoop();
            unsigned long long end = runtime();
            printError("runTime:" + to_string(end - start) + ",aStarTime:" + to_string(strategy.searchTime) +
                       ",maxScore:" +
                       to_string(strategy.maxScore).substr(0, to_string(strategy.maxScore).length() - 3) +
                       ",curScore:" +
                       to_string(strategy.curScore).substr(0, to_string(strategy.curScore).length() - 3));
        }
    } else {
        strategy.init();
        strategy.mainLoop();
    }

    return 0;
}
