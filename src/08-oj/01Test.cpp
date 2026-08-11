#include <iostream>
#include <array>
using namespace std;
/*
某国的货币系统包含面值1元、4元、16元、64元共计4种硬币，以及面值1024元的纸币。现在李明
使用1024元的纸币购买了一件价值为N(0<N≤1024)的商品，
请问最少他会收到多少枚硬币？
输入：
一行，包含一个数N。
输出：
一行，包含一个数，表示最少收到的硬币数。
输入例子：
200
输出例子：
17
例子说明：
花200，需要找零824块，找12个64元硬币，3个16元硬币，2个4元硬币即可。
*/
std::array<int, 4> coins = { 64, 16, 4, 1 };
int coinChange(int rest, int idx) {
    if (rest > coins[idx])
    {
        rest -= coins[idx];
        return 1 + coinChange(rest, idx);
    }
    else if (rest > 0)
    {
    return coinChange(rest, + idx);
    }
    return 1;
}

int main(){
    int n;
    cin >>n;
    int ans = coinChange(1024 - n, 0);
    cout<<ans<<endl;
    return 0;
}