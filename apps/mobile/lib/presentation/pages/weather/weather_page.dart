import 'package:flutter/material.dart';
import '../../../core/managers/ble_manager.dart';
import '../../../core/managers/storage_manager.dart';
import '../../../core/weather/weather_controller.dart';
import '../../../core/weather/weather_service.dart';
import '../../widgets/app_page.dart';

class BleWeatherWatchGateway extends ChangeNotifier
    implements WeatherWatchGateway {
  BleWeatherWatchGateway(this.ble) {
    for (final source in _sources) {
      source.addListener(notifyListeners);
    }
  }
  final BleManager ble;
  List<Listenable> get _sources => [
        ble.isConnected,
        ble.isAuthenticated,
        ble.pairTransferStatus,
      ];
  @override
  bool get ready => ble.isProductReady;
  @override
  Future<String> request(String topic, String value) async {
    if (!ready) {
      throw StateError('Weather sync requires a verified product connection');
    }
    final reply = await ble.requestDeviceChecked(topic, value);
    final id = int.tryParse(reply.requestId ?? '');
    if (reply.topic != topic || id == null || id < 1 || id > 65535) {
      throw const FormatException('Uncorrelated weather reply');
    }
    return reply.value;
  }

  @override
  void dispose() {
    for (final source in _sources) {
      source.removeListener(notifyListeners);
    }
    super.dispose();
  }
}

class WeatherPage extends StatefulWidget {
  const WeatherPage({super.key, this.controller});
  final WeatherController? controller;
  @override
  State<WeatherPage> createState() => _WeatherPageState();
}

class _WeatherPageState extends State<WeatherPage> {
  late final WeatherController _controller;
  late final bool _owned;
  late final TextEditingController _search;
  late final TextEditingController _latitude;
  late final TextEditingController _longitude;
  int _day = 0;

  @override
  void initState() {
    super.initState();
    _owned = widget.controller == null;
    if (widget.controller case final controller?) {
      _controller = controller;
    } else {
      final gateway = BleWeatherWatchGateway(BleManager());
      _controller = WeatherController(
        service: OpenMeteoWeatherService(),
        store: HiveWeatherStore(StorageManager()),
        watch: gateway,
      );
    }
    _search = TextEditingController();
    _latitude = TextEditingController();
    _longitude = TextEditingController();
  }

  @override
  void dispose() {
    _search.dispose();
    _latitude.dispose();
    _longitude.dispose();
    if (_owned) {
      final gateway = _controller.watch as BleWeatherWatchGateway;
      _controller.service.close();
      _controller.dispose();
      gateway.dispose();
    }
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => AnimatedBuilder(
        animation: _controller,
        builder: (context, _) => AppPage(
          title: '时间与天气',
          child: ListView(
            padding: const EdgeInsets.only(top: 16, bottom: 36),
            children: [
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(18),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text('手表时间',
                          style: Theme.of(context).textTheme.titleLarge),
                      const SizedBox(height: 4),
                      const Text('使用手机系统时间和本地时区向手表校时。'),
                      const SizedBox(height: 12),
                      Text(_controller.timeMessage,
                          key: const Key('weather-time-status')),
                      if (_controller.lastTimeSync case final syncedAt?)
                        Padding(
                          padding: const EdgeInsets.only(top: 4),
                          child: Text(
                            '上次确认：${_formatLocalTime(syncedAt)}',
                            key: const Key('weather-time-last-success'),
                            style: Theme.of(context).textTheme.bodySmall,
                          ),
                        ),
                      Align(
                        alignment: Alignment.centerRight,
                        child: FilledButton.icon(
                          key: const Key('weather-sync-time'),
                          onPressed: _controller.ready && !_controller.busy
                              ? _controller.syncTime
                              : null,
                          icon: const Icon(Icons.schedule),
                          label: Text(
                              _controller.timeSyncing ? '正在同步时间…' : '同步手表时间'),
                        ),
                      ),
                      if (_controller.timeSyncing)
                        const LinearProgressIndicator(),
                    ],
                  ),
                ),
              ),
              const SizedBox(height: 12),
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(18),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text('预报位置',
                          style: Theme.of(context).textTheme.titleLarge),
                      const SizedBox(height: 4),
                      Text(_controller.location?.name ?? '尚未选择城市'),
                      const SizedBox(height: 12),
                      Row(
                        children: [
                          Expanded(
                            child: TextField(
                              controller: _search,
                              textInputAction: TextInputAction.search,
                              onSubmitted: (_) =>
                                  _controller.search(_search.text),
                              decoration: const InputDecoration(
                                labelText: '搜索城市',
                                prefixIcon: Icon(Icons.search),
                              ),
                            ),
                          ),
                          const SizedBox(width: 8),
                          IconButton.filledTonal(
                            key: const Key('weather-search'),
                            onPressed: _controller.searching
                                ? null
                                : () => _controller.search(_search.text),
                            icon: const Icon(Icons.arrow_forward),
                          ),
                        ],
                      ),
                      if (_controller.results.isNotEmpty) ...[
                        const SizedBox(height: 8),
                        for (final location in _controller.results)
                          ListTile(
                            key: ValueKey(
                              'weather-location-${location.latitude}-${location.longitude}',
                            ),
                            contentPadding: EdgeInsets.zero,
                            leading: const Icon(Icons.location_on_outlined),
                            title: Text(location.name),
                            subtitle: Text(
                              '${location.latitude.toStringAsFixed(3)}, ${location.longitude.toStringAsFixed(3)}',
                            ),
                            onTap: () async {
                              await _controller.select(location);
                              if (mounted) _search.clear();
                            },
                          ),
                      ],
                      if (_controller.results.isEmpty)
                        ExpansionTile(
                          key: const Key('weather-manual-location'),
                          tilePadding: EdgeInsets.zero,
                          title: const Text('手动输入坐标'),
                          subtitle: const Text('城市搜索无结果时可直接指定经纬度'),
                          children: [
                            Row(children: [
                              Expanded(
                                  child: TextField(
                                      key: const Key('weather-latitude'),
                                      controller: _latitude,
                                      keyboardType:
                                          const TextInputType.numberWithOptions(
                                              decimal: true, signed: true),
                                      decoration: const InputDecoration(
                                          labelText: '纬度 −90…90'))),
                              const SizedBox(width: 10),
                              Expanded(
                                  child: TextField(
                                      key: const Key('weather-longitude'),
                                      controller: _longitude,
                                      keyboardType:
                                          const TextInputType.numberWithOptions(
                                              decimal: true, signed: true),
                                      decoration: const InputDecoration(
                                          labelText: '经度 −180…180'))),
                            ]),
                            Align(
                                alignment: Alignment.centerRight,
                                child: TextButton.icon(
                                    key: const Key('weather-save-coordinates'),
                                    onPressed: _saveCoordinates,
                                    icon: const Icon(Icons.save_outlined),
                                    label: const Text('保存坐标'))),
                          ],
                        ),
                    ],
                  ),
                ),
              ),
              const SizedBox(height: 12),
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(18),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Row(
                        children: [
                          Expanded(
                            child: Text(
                              '天气预报',
                              style: Theme.of(context).textTheme.titleLarge,
                            ),
                          ),
                          IconButton(
                            tooltip: '刷新天气',
                            key: const Key('weather-refresh'),
                            onPressed:
                                _controller.busy || _controller.location == null
                                    ? null
                                    : _controller.refresh,
                            icon: const Icon(Icons.refresh),
                          ),
                        ],
                      ),
                      Text(_controller.message,
                          key: const Key('weather-status')),
                      if (_controller.fetchedAt != null &&
                          _controller.forecast != null)
                        Text(
                          '缓存位置：${_controller.cachedLocation?.name ?? '未知'} · ${_controller.fetchedAt!.toLocal().toString().substring(0, 16)} · ${_controller.cacheFresh ? '30 分钟内' : '已过期'}',
                          key: const Key('weather-cache-age'),
                          style: Theme.of(context).textTheme.bodySmall,
                        ),
                      if (_controller.busy) const LinearProgressIndicator(),
                      if (_controller.hasCurrentForecast)
                        ..._forecast(context, _controller.forecast!),
                      const SizedBox(height: 12),
                      Wrap(
                        spacing: 8,
                        runSpacing: 8,
                        children: [
                          OutlinedButton.icon(
                            key: const Key('weather-fetch'),
                            onPressed:
                                _controller.busy || _controller.location == null
                                    ? null
                                    : _controller.refresh,
                            icon: const Icon(Icons.cloud_download_outlined),
                            label: const Text('仅更新手机天气'),
                          ),
                          FilledButton.icon(
                            key: const Key('weather-sync'),
                            onPressed: _controller.busy ||
                                    !_controller.ready ||
                                    _controller.location == null
                                ? null
                                : _controller.sync,
                            icon: const Icon(Icons.sync),
                            label:
                                Text(_controller.ready ? '同步到手表' : '连接并验证后同步'),
                          ),
                        ],
                      ),
                    ],
                  ),
                ),
              ),
              const Padding(
                padding: EdgeInsets.only(top: 14),
                child: Text(
                  '天气数据：Open-Meteo（open-meteo.com）。新连接就绪时，有已保存城市则自动同步天气，否则只同步时间。天气缓存 30 分钟内复用，过期则重新获取；手表离线时可单独更新 App 天气。',
                ),
              ),
            ],
          ),
        ),
      );

  List<Widget> _forecast(BuildContext context, WeatherForecast forecast) {
    final day = forecast.days[_day.clamp(0, forecast.days.length - 1)];
    final date =
        '${day.date.year}-${day.date.month.toString().padLeft(2, '0')}-${day.date.day.toString().padLeft(2, '0')}';
    return [
      const Divider(height: 24),
      Text(date, style: Theme.of(context).textTheme.headlineSmall),
      const SizedBox(height: 8),
      Text(
        _weatherDescription(day.code),
        style: Theme.of(context).textTheme.titleLarge,
      ),
      const SizedBox(height: 4),
      Text(
        '最低 ${day.lowC.toStringAsFixed(1)}°C   最高 ${day.highC.toStringAsFixed(1)}°C',
      ),
      Text('降水概率 ${day.rainProbability}% · ${forecast.timezone}'),
      if (forecast.days.length > 1)
        Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            IconButton(
              key: const Key('weather-previous'),
              onPressed: _day > 0 ? () => setState(() => _day--) : null,
              icon: const Icon(Icons.chevron_left),
            ),
            Text('${_day + 1} / ${forecast.days.length} 天'),
            IconButton(
              key: const Key('weather-next'),
              onPressed: _day + 1 < forecast.days.length
                  ? () => setState(() => _day++)
                  : null,
              icon: const Icon(Icons.chevron_right),
            ),
          ],
        ),
    ];
  }

  Future<void> _saveCoordinates() async {
    final lat = double.tryParse(_latitude.text.trim());
    final lon = double.tryParse(_longitude.text.trim());
    if (lat == null ||
        lon == null ||
        !lat.isFinite ||
        !lon.isFinite ||
        lat < -90 ||
        lat > 90 ||
        lon < -180 ||
        lon > 180) {
      ScaffoldMessenger.of(context)
          .showSnackBar(const SnackBar(content: Text('请输入有效的纬度和经度。')));
      return;
    }
    await _controller.select(WeatherLocation(
        name: '${lat.toStringAsFixed(3)}, ${lon.toStringAsFixed(3)}',
        latitude: lat,
        longitude: lon));
  }
}

String _weatherDescription(int code) => switch (code) {
      0 => '晴朗',
      1 => '大致晴朗',
      2 => '局部多云',
      3 => '阴天',
      45 || 48 => '有雾',
      51 || 53 || 55 || 56 || 57 => '毛毛雨',
      61 || 63 || 65 || 66 || 67 => '降雨',
      71 || 73 || 75 || 77 => '降雪',
      80 || 81 || 82 => '阵雨',
      85 || 86 => '阵雪',
      95 || 96 || 99 => '雷暴',
      _ => '天气代码 $code',
    };

String _formatLocalTime(DateTime value) {
  final local = value.toLocal();
  String twoDigits(int number) => number.toString().padLeft(2, '0');
  return '${local.year}-${twoDigits(local.month)}-${twoDigits(local.day)} '
      '${twoDigits(local.hour)}:${twoDigits(local.minute)}';
}
