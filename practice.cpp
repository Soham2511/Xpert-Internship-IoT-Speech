#include <bits/stdc++.h>
using namespace std;
using ll = long long;

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    int T;
    if (!(cin >> T)) return 0;
    while (T--) {
        int n;
        cin >> n;
        vector<ll> a(n), c(n);
        ll total = 0;
        for (int i = 0; i < n; ++i) cin >> a[i];
        for (int i = 0; i < n; ++i) { cin >> c[i]; total += c[i]; }

        // dp[i] = maximum sum of costs of a non-decreasing subsequence that ends at i (i is kept)
        vector<ll> dp(n, 0);
        ll best = 0;
        for (int i = 0; i < n; ++i) {
            dp[i] = c[i]; // keep only i
            for (int j = 0; j < i; ++j) {
                if (a[j] <= a[i]) {
                    dp[i] = max(dp[i], dp[j] + c[i]);
                }
            }
            best = max(best, dp[i]);
        }

        ll answer = total - best;
        cout << answer << '\n';
    }
    return 0;
}
