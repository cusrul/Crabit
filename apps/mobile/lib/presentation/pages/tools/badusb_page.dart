import 'dart:async';

import 'package:flutter/material.dart';
import 'package:hive_flutter/hive_flutter.dart';

import '../../../core/usb/usb_device_client.dart';
import '../../../core/usb/usb_device_gateway.dart';
import '../../../core/usb/usb_script_protocol.dart';
import '../../../core/scripts/usb_script.dart';
import '../../../core/scripts/usb_script_composer.dart';
import '../../../core/scripts/usb_script_repository.dart';
import '../../../core/managers/storage_manager.dart';
import '../../../core/managers/ble_manager.dart' show bleTimingEnabled;

const _examples = <UsbScript>[
  UsbScript(
      id: 'example-text', title: '输入字符', body: 'STRING Hello from Crabit'),
  UsbScript(id: 'example-delay', title: '等待 100ms', body: 'DELAY 100'),
  UsbScript(id: 'example-enter', title: '回车', body: 'ENTER'),
];

class BadUsbPage extends StatefulWidget {
  const BadUsbPage({super.key, this.repository, this.gateway});

  /// Injected for deterministic tests; production uses the initialized settings box.
  final UsbScriptRepository? repository;
  final UsbDeviceGateway? gateway;

  @override
  State<BadUsbPage> createState() => _BadUsbPageState();
}

class _BadUsbPageState extends State<BadUsbPage>
    with SingleTickerProviderStateMixin, WidgetsBindingObserver {
  late final TabController _tabController;
  final _scriptController = TextEditingController();
  final _nameController = TextEditingController();
  final _liveBufferController = TextEditingController();
  late final UsbScriptRepository _repository;
  late final UsbDeviceGateway _gateway;
  late final UsbDeviceClient _deviceClient;
  List<UsbScript> _scripts = [];
  bool _loading = true;
  bool _saving = false;
  String? _error;
  String? _editingScriptId;
  UsbDeviceReply? _deviceReply;
  String? _deviceError;
  String? _deviceProgress;
  bool _deviceBusy = false;
  bool _appVisible = true;
  bool _pageVisible = true;
  Timer? _statusPoll;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _tabController = TabController(length: 3, vsync: this)
      ..addListener(_syncStatusPolling);
    _scriptController.addListener(_onDraftChanged);
    _repository = widget.repository ??
        HiveUsbScriptRepository(Hive.box(StorageManager.boxSettings));
    _gateway = widget.gateway ?? BleUsbDeviceGateway();
    _deviceClient = UsbDeviceClient(gateway: _gateway);
    _loadScripts();
  }

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    _pageVisible = TickerMode.valuesOf(context).enabled;
    _syncStatusPolling();
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    _appVisible = state == AppLifecycleState.resumed;
    _syncStatusPolling();
  }

  void _onDraftChanged() {
    if (mounted) setState(() {});
  }

  Future<void> _loadScripts() async {
    try {
      final scripts = await _repository.load();
      if (!mounted) return;
      setState(() {
        _scripts = scripts;
        _loading = false;
      });
    } catch (error) {
      if (!mounted) return;
      setState(() {
        _loading = false;
        _error = '无法读取本地脚本：$error';
      });
    }
  }

  @override
  void dispose() {
    _scriptController.removeListener(_onDraftChanged);
    _scriptController.dispose();
    _nameController.dispose();
    _liveBufferController.dispose();
    _statusPoll?.cancel();
    _tabController.removeListener(_syncStatusPolling);
    _tabController.dispose();
    WidgetsBinding.instance.removeObserver(this);
    super.dispose();
  }

  void _syncStatusPolling() {
    final activeState = _deviceReply?.state == UsbRamState.running ||
        _deviceReply?.state == UsbRamState.stopping;
    final shouldPoll = mounted && _appVisible && _pageVisible && activeState;
    if (shouldPoll && _statusPoll == null) {
      _statusPoll = Timer.periodic(const Duration(seconds: 2), (_) {
        final routeVisible = ModalRoute.of(context)?.isCurrent ?? true;
        if (routeVisible && !_deviceBusy) {
          unawaited(_refreshDeviceStatus(showError: false));
        }
      });
    } else if (!shouldPoll) {
      _statusPoll?.cancel();
      _statusPoll = null;
    }
  }

  Future<void> _performDeviceAction(
    Future<UsbDeviceReply> Function() operation, {
    bool showError = true,
  }) async {
    if (_deviceBusy) return;
    setState(() {
      _deviceBusy = true;
      _deviceError = null;
    });
    try {
      final reply = await operation();
      if (!mounted) return;
      setState(() => _deviceReply = reply);
    } catch (error) {
      if (!mounted) return;
      setState(() {
        _deviceReply = _deviceClient.lastReply ?? _deviceReply;
        if (showError) _deviceError = 'USB 设备操作未完成：$error';
      });
    } finally {
      if (mounted) {
        setState(() {
          _deviceBusy = false;
          _deviceReply = _deviceClient.lastReply ?? _deviceReply;
        });
        _syncStatusPolling();
      }
    }
  }

  Future<void> _refreshDeviceStatus({bool showError = true}) =>
      _performDeviceAction(_deviceClient.status, showError: showError);

  Future<void> _uploadDraft() =>
      _performDeviceAction(() => _deviceClient.upload(_scriptController.text));

  Future<void> _runLocalScript(String source) async {
    if (_deviceBusy || !_gateway.isProductReady) return;
    try {
      validateUsbScript(source);
    } catch (error) {
      setState(() => _deviceError = '脚本无法运行：$error');
      return;
    }
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('运行到 Crabit？'),
        content: const Text(
            '将自动上传并校验，然后通过手表 USB 键盘执行一次。请确认连接的是自己的主机，并准备好目标窗口。建议脚本开头加入 DELAY 3000，留出切换窗口的时间。'),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(context, false),
              child: const Text('取消')),
          FilledButton(
              onPressed: () => Navigator.pop(context, true),
              child: const Text('运行一次')),
        ],
      ),
    );
    if (confirmed != true || !mounted || _deviceBusy) return;
    var executionRequested = false;
    final operationWatch = bleTimingEnabled ? (Stopwatch()..start()) : null;
    await _performDeviceAction(() async {
      setState(() => _deviceProgress = '检查设备连接…');
      final status = await _deviceClient.status();
      if (!status.usbConnected) throw StateError('手表 USB 尚未连接主机。');
      if ({UsbRamState.running, UsbRamState.stopping}.contains(status.state)) {
        throw StateError('设备仍在运行或停止上一个脚本。');
      }
      if (!mounted) throw StateError('页面已关闭，未执行。');
      setState(() => _deviceProgress = '上传并校验脚本…');
      final uploaded = await _deviceClient.upload(source);
      if (!mounted) throw StateError('页面已关闭，未执行。');
      setState(() => _deviceProgress = '请求执行…');
      executionRequested = true;
      try {
        final reply = await _deviceClient.run(uploaded.id);
        if (mounted) setState(() => _deviceProgress = '脚本运行中');
        return reply;
      } catch (_) {
        if (mounted) setState(() => _deviceProgress = '执行结果未知，请查询状态；不会自动重试');
        throw StateError('执行结果未知，请查询状态；不会自动重试。');
      }
    });
    if (mounted && !executionRequested) setState(() => _deviceProgress = null);
    if (operationWatch != null) {
      debugPrint(
          '[USB_TIMING] workflow us=${operationWatch.elapsedMicroseconds} runRequested=$executionRequested');
    }
  }

  Future<void> _runDeviceScript() async {
    final reply = _deviceReply;
    if (reply == null ||
        reply.state != UsbRamState.ready ||
        !_deviceClient.canRun(reply.id)) {
      return;
    }
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('确认运行键盘脚本'),
        content: const Text(
          '脚本将通过 USB 键盘向连接的主机发送按键。请先将输入焦点放到目标窗口，并确认主机已准备好接收输入。此功能只发送键盘按键，不控制鼠标。',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text('取消'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, true),
            child: const Text('运行一次'),
          ),
        ],
      ),
    );
    if (confirmed != true || !mounted) return;
    await _performDeviceAction(() => _deviceClient.run(reply.id));
  }

  Future<void> _stopDeviceScript() async {
    final id = _deviceReply?.id;
    if (id == null || id == '00000000') return;
    await _performDeviceAction(() => _deviceClient.stop(id));
  }

  Future<void> _clearDeviceScript() async {
    final id = _deviceReply?.id;
    if (id == null || id == '00000000') return;
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('清除设备 RAM 脚本？'),
        content: const Text('这只会清除手表 RAM 中当前脚本，不会擦写 Flash。'),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text('取消'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, true),
            child: const Text('清除 RAM'),
          ),
        ],
      ),
    );
    if (confirmed != true || !mounted) return;
    await _performDeviceAction(() => _deviceClient.clear(id));
  }

  Future<void> _saveScript() async {
    final body = _scriptController.text;
    if (body.trim().isEmpty || _saving) return;
    final current = _scripts.where((script) => script.id == _editingScriptId);
    _nameController.text = current.isEmpty ? '' : current.first.title;
    final name = await showDialog<String>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('保存到本地脚本库'),
        content: TextField(
          controller: _nameController,
          autofocus: true,
          maxLength: 60,
          decoration: const InputDecoration(labelText: '脚本名称'),
          onSubmitted: (value) => Navigator.pop(context, value),
        ),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(context), child: const Text('取消')),
          FilledButton(
              onPressed: () => Navigator.pop(context, _nameController.text),
              child: const Text('保存')),
        ],
      ),
    );
    if (name == null || name.trim().isEmpty || !mounted) return;
    final id =
        _editingScriptId ?? DateTime.now().microsecondsSinceEpoch.toString();
    await _persist(() => _repository.save(UsbScript(
          id: id,
          title: name.trim(),
          body: body,
        )));
  }

  Future<void> _deleteScript(UsbScript script) async {
    await _persist(() => _repository.delete(script.id));
  }

  Future<void> _persist(Future<void> Function() operation) async {
    setState(() {
      _saving = true;
      _error = null;
    });
    try {
      await operation();
      final scripts = await _repository.load();
      if (mounted) {
        setState(() {
          _scripts = scripts;
          _error = null;
        });
        ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
          content: Text('已保存到手机本地脚本库；未发送或执行。'),
        ));
      }
    } catch (error) {
      if (mounted) setState(() => _error = '本地保存失败：$error');
    } finally {
      if (mounted) setState(() => _saving = false);
    }
  }

  void _loadDraft(UsbScript script, {bool editExisting = false}) {
    setState(() {
      _editingScriptId = editExisting ? script.id : null;
      _scriptController.value = TextEditingValue(
        text: script.body,
        selection: TextSelection.collapsed(offset: script.body.length),
      );
    });
    _tabController.animateTo(0);
  }

  void _insert(String text) {
    final value = _scriptController.value;
    final selection = value.selection;
    final insertion = insertUsbScriptFragment(value.text, text,
        start: selection.start, end: selection.end);
    _editingScriptId =
        null; // A composition is a new script, not a source overwrite.
    _scriptController.value = TextEditingValue(
      text: insertion.text,
      selection: TextSelection.collapsed(offset: insertion.cursor),
    );
  }

  @override
  Widget build(BuildContext context) => Scaffold(
        appBar: AppBar(
          title: const Text('USB 工具'),
          bottom: TabBar(controller: _tabController, tabs: const [
            Tab(icon: Icon(Icons.edit_note), text: '脚本工作台'),
            Tab(icon: Icon(Icons.keyboard), text: '文本预览'),
            Tab(
                key: ValueKey('usb-device-tab'),
                icon: Icon(Icons.usb),
                text: '设备 RAM'),
          ]),
        ),
        body: Column(children: [
          _notice(),
          if (_deviceProgress != null || _deviceError != null)
            Padding(
              padding: const EdgeInsets.all(12),
              child: Row(children: [
                Expanded(
                    child: Text(_deviceError ??
                        (_deviceReply?.state == UsbRamState.done
                            ? '脚本执行完成'
                            : _deviceProgress!))),
                TextButton(
                    onPressed: _deviceBusy ? null : _refreshDeviceStatus,
                    child: const Text('查询状态')),
                OutlinedButton.icon(
                    onPressed: !_deviceBusy &&
                            {
                              UsbRamState.ready,
                              UsbRamState.running,
                              UsbRamState.stopping
                            }.contains(_deviceReply?.state)
                        ? _stopDeviceScript
                        : null,
                    icon: const Icon(Icons.stop),
                    label: const Text('停止')),
              ]),
            ),
          if (_error != null) _errorBanner(),
          Expanded(
            child: TabBarView(
                controller: _tabController,
                physics: const NeverScrollableScrollPhysics(),
                children: [
                  _workbench(),
                  _textPreview(),
                  _deviceControls(),
                ]),
          ),
        ]),
      );

  Widget _notice() => Container(
        width: double.infinity,
        margin: const EdgeInsets.fromLTRB(16, 12, 16, 0),
        padding: const EdgeInsets.all(12),
        decoration: BoxDecoration(
          color: Theme.of(context).colorScheme.secondaryContainer,
          borderRadius: BorderRadius.circular(12),
        ),
        child: const Text(
          '本地脚本可直接运行到 Crabit：自动上传、校验后执行一次。脚本只放设备 RAM，不写 Flash；高级操作见设备 RAM。',
        ),
      );

  Widget _errorBanner() => MaterialBanner(
        content: Text(_error!),
        leading: const Icon(Icons.error_outline),
        actions: [
          TextButton(
              onPressed: () => setState(() => _error = null),
              child: const Text('关闭'))
        ],
      );

  Widget _workbench() => LayoutBuilder(builder: (context, constraints) {
        if (constraints.maxWidth >= 900) {
          return Row(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
            Expanded(flex: 3, child: _editor()),
            const VerticalDivider(width: 1),
            SizedBox(width: 340, child: _library()),
          ]);
        }
        return Column(children: [
          Expanded(flex: 3, child: _editor()),
          const Divider(height: 1),
          Expanded(flex: 2, child: _library()),
        ]);
      });

  Widget _editor() => Padding(
        padding: const EdgeInsets.all(16),
        child:
            Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
          Text('脚本草稿', style: Theme.of(context).textTheme.titleMedium),
          const SizedBox(height: 8),
          Wrap(spacing: 8, runSpacing: 8, children: [
            FilledButton.tonalIcon(
              onPressed: _saving ? null : _saveScript,
              icon: const Icon(Icons.save_outlined),
              label: Text(_saving
                  ? '保存中…'
                  : _editingScriptId == null
                      ? '保存到本地'
                      : '更新本地脚本'),
            ),
            FilledButton.icon(
                onPressed: _deviceBusy || !_gateway.isProductReady
                    ? null
                    : () => _runLocalScript(_scriptController.text),
                icon: const Icon(Icons.play_arrow),
                label: const Text('运行到 Crabit')),
          ]),
          const SizedBox(height: 12),
          Expanded(
            child: TextField(
              key: const ValueKey('usb-script-editor'),
              controller: _scriptController,
              expands: true,
              maxLines: null,
              textAlignVertical: TextAlignVertical.top,
              style: const TextStyle(fontFamily: 'monospace'),
              decoration: const InputDecoration(
                hintText: '在此编辑脚本草稿…',
                alignLabelWithHint: true,
                border: OutlineInputBorder(),
              ),
            ),
          ),
        ]),
      );

  Widget _deviceControls() {
    final draft = _scriptController.text;
    ValidatedUsbScript? validation;
    String? validationError;
    try {
      validation = validateUsbScript(draft);
    } on UsbScriptValidationException catch (error) {
      validationError = error.message;
    }
    final reply = _deviceReply;
    final runEnabled = reply != null &&
        reply.state == UsbRamState.ready &&
        _deviceClient.canRun(reply.id);
    final stopEnabled = reply != null &&
        reply.state == UsbRamState.running &&
        reply.hasScript &&
        reply.id == _deviceClient.scriptId &&
        !_deviceBusy &&
        _gateway.isProductReady;
    final clearEnabled = reply != null &&
        reply.hasScript &&
        reply.id == _deviceClient.scriptId &&
        reply.state != UsbRamState.running &&
        reply.state != UsbRamState.stopping &&
        !_deviceBusy &&
        _gateway.isProductReady;

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        Text('设备 RAM 控制', style: Theme.of(context).textTheme.titleLarge),
        const SizedBox(height: 8),
        const Text(
          '仅键盘输入；采用 US 键盘布局映射。上传上限 512 字节、128 个动作、总延时 30 秒。请检查内容，并将主机输入焦点放在目标窗口。',
        ),
        const SizedBox(height: 12),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(12),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text('设备状态：${reply?.state.name ?? '未知'}'),
                if (reply != null) ...[
                  Text('脚本 ID：${reply.id}'),
                  Text('RAM：${reply.received}/${reply.length} 字节'),
                  Text('USB 主机：${reply.usbConnected ? '已连接' : '未连接'}'),
                  if (reply.error != 'none') Text('设备错误：${reply.error}'),
                ],
                if (_deviceBusy) const LinearProgressIndicator(),
              ],
            ),
          ),
        ),
        if (!_gateway.isProductReady)
          const Padding(
            padding: EdgeInsets.only(bottom: 8),
            child: Text('设备控制需要已验证的产品连接。'),
          ),
        if (_deviceError != null)
          Padding(
            padding: const EdgeInsets.only(bottom: 8),
            child: Text(
              _deviceError!,
              style: TextStyle(color: Theme.of(context).colorScheme.error),
            ),
          ),
        if (validationError != null)
          Padding(
            padding: const EdgeInsets.only(bottom: 8),
            child: Text(validationError),
          )
        else if (validation != null)
          Padding(
            padding: const EdgeInsets.only(bottom: 8),
            child: Text(
                '当前草稿：${validation.bytes.length}/512 字节，${validation.actions}/128 个动作，预计总时长 ${validation.totalDurationMs} ms。'),
          ),
        Wrap(
          spacing: 8,
          runSpacing: 8,
          children: [
            OutlinedButton.icon(
              key: const ValueKey('usb-status-button'),
              onPressed: _deviceBusy || !_gateway.isProductReady
                  ? null
                  : _refreshDeviceStatus,
              icon: const Icon(Icons.refresh),
              label: const Text('查询状态'),
            ),
            FilledButton.tonalIcon(
              key: const ValueKey('usb-upload-button'),
              onPressed:
                  _deviceBusy || !_gateway.isProductReady || validation == null
                      ? null
                      : _uploadDraft,
              icon: const Icon(Icons.upload),
              label: const Text('上传到设备 RAM'),
            ),
            FilledButton.icon(
              key: const ValueKey('usb-run-button'),
              onPressed: runEnabled ? _runDeviceScript : null,
              icon: const Icon(Icons.play_arrow),
              label: const Text('运行一次'),
            ),
            OutlinedButton.icon(
              onPressed: stopEnabled ? _stopDeviceScript : null,
              icon: const Icon(Icons.stop),
              label: const Text('停止'),
            ),
            TextButton.icon(
              onPressed: clearEnabled ? _clearDeviceScript : null,
              icon: const Icon(Icons.delete_outline),
              label: const Text('清除 RAM'),
            ),
          ],
        ),
      ],
    );
  }

  Widget _library() {
    if (_loading) return const Center(child: CircularProgressIndicator());
    final items = [..._examples, ..._scripts];
    return ListView(
      key: const ValueKey('usb-script-library'),
      padding: const EdgeInsets.all(16),
      children: [
        Text('本地脚本', style: Theme.of(context).textTheme.titleMedium),
        const SizedBox(height: 8),
        const Text('载入会插入编辑器并自动换行，可重复组合。保存草稿即可创建自己的动作片段或完整脚本。'),
        const SizedBox(height: 12),
        if (items.isEmpty)
          const Padding(
            padding: EdgeInsets.all(24),
            child: Center(child: Text('还没有本地脚本。可在编辑器中创建并保存草稿。')),
          ),
        for (final script in items)
          Card(
            child: Column(
              key: ValueKey('script-${script.id}'),
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                ListTile(
                  leading: Icon(_examples.any((item) => item.id == script.id)
                      ? Icons.info_outline
                      : Icons.description_outlined),
                  title: Text(script.title),
                  subtitle: Text(script.body.replaceAll('\n', '  ·  '),
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(fontFamily: 'monospace')),
                ),
                Padding(
                    padding: const EdgeInsets.fromLTRB(12, 0, 12, 12),
                    child: Wrap(spacing: 8, runSpacing: 8, children: [
                      FilledButton.tonalIcon(
                          key: ValueKey('insert-local-${script.id}'),
                          onPressed: () => _insert(script.body),
                          icon: const Icon(Icons.add),
                          label: const Text('载入编辑器')),
                      IconButton.filledTonal(
                          key: ValueKey('run-local-${script.id}'),
                          onPressed: _deviceBusy || !_gateway.isProductReady
                              ? null
                              : () => _runLocalScript(script.body),
                          icon: const Icon(Icons.play_arrow),
                          tooltip: '运行到 Crabit'),
                      if (!_examples.any((item) => item.id == script.id))
                        TextButton.icon(
                          onPressed: () => _loadDraft(script,
                              editExisting: !_examples
                                  .any((item) => item.id == script.id)),
                          icon: const Icon(Icons.edit_outlined),
                          label: const Text('编辑原脚本'),
                        ),
                      if (!_examples.any((item) => item.id == script.id))
                        IconButton(
                            tooltip: '删除本地脚本',
                            onPressed:
                                _saving ? null : () => _deleteScript(script),
                            icon: const Icon(Icons.delete_outline)),
                    ])),
              ],
            ),
          ),
      ],
    );
  }

  Widget _textPreview() => Padding(
        padding: const EdgeInsets.all(16),
        child:
            Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
          Text('固定文本组合预览', style: Theme.of(context).textTheme.titleLarge),
          const SizedBox(height: 8),
          const Text('此处只在本机组合文本，不会发送键盘报告。需要让手表执行时，请使用脚本工作台。'),
          const SizedBox(height: 16),
          TextField(
            controller: _liveBufferController,
            minLines: 2,
            maxLines: 4,
            decoration: const InputDecoration(
                labelText: '本地文本草稿', border: OutlineInputBorder()),
          ),
          const SizedBox(height: 16),
          Wrap(spacing: 8, runSpacing: 8, children: [
            for (final char in '1234567890QWERTYUIOPASDFGHJKLZXCVBNM'.split(''))
              OutlinedButton(
                onPressed: () => _liveBufferController.text += char,
                child: Text(char),
              ),
            OutlinedButton(
                onPressed: () => _liveBufferController.text += ' ',
                child: const Text('空格')),
            OutlinedButton(
                onPressed: () => _liveBufferController.text += '\n',
                child: const Text('换行')),
            TextButton.icon(
                onPressed: () => _liveBufferController.clear(),
                icon: const Icon(Icons.clear),
                label: const Text('清空')),
          ]),
        ]),
      );
}
