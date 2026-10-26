import 'package:flutter/material.dart';

class WifiSecurityPage extends StatelessWidget {
  const WifiSecurityPage({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text(
          'Wi-Fi 协议安全分析',
          style: TextStyle(fontWeight: FontWeight.bold, fontSize: 18),
        ),
      ),
      body: ListView(
        padding: const EdgeInsets.all(16.0),
        physics: const BouncingScrollPhysics(),
        children: [
          _buildDisclaimerCard(),
          const SizedBox(height: 24),
          _buildSectionHeader('WPA2 攻击链路原理解析', Icons.hub, Colors.cyanAccent),
          const SizedBox(height: 12),
          const Padding(
            padding: EdgeInsets.symmetric(horizontal: 4.0, vertical: 8.0),
            child: Text(
              '核心在于利用 802.11 协议在管理帧上的历史遗留漏洞，迫使客户端重新执行完整的安全认证过程，这是经典离线字典攻击的必备前置步骤。',
              style:
                  TextStyle(color: Colors.white70, fontSize: 14, height: 1.5),
            ),
          ),
          _buildStepCard(
            step: '01',
            title: '扫描与目标锁定',
            subtitle: 'Reconnaissance',
            icon: Icons.radar,
            color: Colors.cyan,
            content: '• 信标帧 (Beacon)：混杂模式下遍历信道，监听 AP 发送的信标帧。\n'
                '• 信息提取：提取 BSSID、目标信道、加密套件信息 (RSN IE)。\n'
                '• 客户端探测：监听数据帧，找出连接在目标 AP 上的活跃客户端 MAC。',
          ),
          _buildStepCard(
            step: '02',
            title: 'Deauth 注入',
            subtitle: 'Disruption',
            icon: Icons.flash_on,
            color: Colors.orangeAccent,
            content: '• 伪造管理帧：构造 802.11 取消身份验证帧 (Type 0, Subtype 12)。\n'
                '• MAC 欺骗：源 MAC 伪装成 AP，目的 MAC 设为客户端或广播地址。\n'
                '• 强制断网：利用标准无加密漏洞，客户端收到后无条件信任并断开。',
          ),
          _buildStepCard(
            step: '03',
            title: '握手包捕获',
            subtitle: 'Extraction',
            icon: Icons.lock_open,
            color: Colors.redAccent,
            content: '• 自动重连：被强制断网的客户端通常会自动尝试重新连接。\n'
                '• 监听 EAPOL 帧：持续监听目标信道，通过 0x888E 过滤并捕获四次握手包。\n'
                '• 离线破解：导出包含 Nonce、MAC 和 MIC 的包，利用 GPU 进行字典爆破比对。',
          ),
          const SizedBox(height: 32),
          _buildSectionHeader('现代网络的防御机制', Icons.security, Colors.greenAccent),
          const SizedBox(height: 12),
          const Padding(
            padding: EdgeInsets.symmetric(horizontal: 4.0, vertical: 8.0),
            child: Text(
              '随着 802.11 协议的演进，上述经典的攻击链路正在逐渐失效。',
              style:
                  TextStyle(color: Colors.white70, fontSize: 14, height: 1.5),
            ),
          ),
          _buildDefenseCard(
            title: '防御 Deauth：802.11w (PMF)',
            subtitle: '受保护的管理帧 (Protected Management Frames)',
            icon: Icons.shield,
            color: Colors.greenAccent,
            content:
                '机制：在设备连接时，双方协商一个完整性组临时密钥 (IGTK)。所有关键的管理帧都必须使用此密钥生成加密的校验和 (MIC)。\n\n'
                '效果：当攻击者注入伪造的 Deauth 帧时，由于没有 IGTK，无法生成正确的 MIC。底层网卡验证失败直接丢弃，网络不受影响。',
          ),
          _buildDefenseCard(
            title: '防御离线破解：WPA3 与 SAE',
            subtitle: '对等同步身份认证 (Simultaneous Authentication of Equals)',
            icon: Icons.enhanced_encryption,
            color: Colors.tealAccent,
            content:
                '机制：WPA3 用基于 Dragonfly 握手的 SAE 取代了 WPA2 的预共享密钥 (PSK) 四次握手。\n\n'
                '效果：SAE 具有前向保密性，且对密码的验证必须通过在线的交互式计算完成。攻击者即使抓包也无法进行无限次离线字典试错。',
          ),
          const SizedBox(height: 40),
        ],
      ),
    );
  }

  Widget _buildDisclaimerCard() {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Colors.blueGrey.withValues(alpha: 0.1),
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: Colors.blueGrey.withValues(alpha: 0.3)),
      ),
      child: Row(
        children: [
          Icon(Icons.info_outline, color: Colors.blueGrey.shade300, size: 28),
          const SizedBox(width: 16),
          const Expanded(
            child: Text(
              "仅限协议分析与安全研究。本实验室从协议底层解释漏洞机制及现代 Wi-Fi 网络的防御原理。",
              style:
                  TextStyle(color: Colors.white70, fontSize: 13, height: 1.4),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildSectionHeader(String title, IconData icon, Color color) {
    return Row(
      children: [
        Icon(icon, color: color, size: 22),
        const SizedBox(width: 8),
        Text(
          title,
          style: TextStyle(
            color: color,
            fontSize: 18,
            fontWeight: FontWeight.bold,
            letterSpacing: 1.1,
          ),
        ),
      ],
    );
  }

  Widget _buildStepCard(
      {required String step,
      required String title,
      required String subtitle,
      required IconData icon,
      required Color color,
      required String content}) {
    return Card(
      margin: const EdgeInsets.symmetric(vertical: 8.0),
      shape: RoundedRectangleBorder(
        side: BorderSide(color: color.withValues(alpha: 0.2), width: 1),
        borderRadius: BorderRadius.circular(16),
      ),
      child: Padding(
        padding: const EdgeInsets.all(16.0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Text(step,
                    style: TextStyle(
                        color: color.withValues(alpha: 0.5),
                        fontSize: 24,
                        fontWeight: FontWeight.w900,
                        fontStyle: FontStyle.italic)),
                const SizedBox(width: 12),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(title,
                          style: const TextStyle(
                              fontSize: 16,
                              fontWeight: FontWeight.bold,
                              color: Colors.white)),
                      Text(subtitle,
                          style: TextStyle(
                              fontSize: 12,
                              color: color.withValues(alpha: 0.8))),
                    ],
                  ),
                ),
                Icon(icon, color: color.withValues(alpha: 0.8)),
              ],
            ),
            const Padding(
                padding: EdgeInsets.symmetric(vertical: 8),
                child: Divider(color: Colors.white10)),
            Text(content,
                style: const TextStyle(
                    color: Colors.white70, fontSize: 13, height: 1.6)),
          ],
        ),
      ),
    );
  }

  Widget _buildDefenseCard(
      {required String title,
      required String subtitle,
      required IconData icon,
      required Color color,
      required String content}) {
    return _buildStepCard(
      step: 'DEF',
      title: title,
      subtitle: subtitle,
      icon: icon,
      color: color,
      content: content,
    );
  }
}
