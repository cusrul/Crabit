/// Pure foreground retry policy. An epoch invalidates both timers and async work.
class ReconnectAttempt {
  const ReconnectAttempt(this.epoch, this.remoteId, this.number, this.delay);
  final int epoch;
  final String remoteId;
  final int number;
  final Duration delay;
}

class ReconnectPolicy {
  static const backoff = [
    Duration(seconds: 1),
    Duration(seconds: 3),
    Duration(seconds: 7),
  ];
  bool enabled = false;
  bool foreground = false;
  bool diagnostic = false;
  bool userStopped = false;
  bool faulted = false;
  String? rememberedId;
  int epoch = 0;
  int attempts = 0;

  bool get eligible =>
      enabled &&
      foreground &&
      !diagnostic &&
      !userStopped &&
      !faulted &&
      (rememberedId?.isNotEmpty ?? false);

  void invalidate({bool resetBudget = false}) {
    epoch++;
    if (resetBudget) attempts = 0;
  }

  ReconnectAttempt? next() {
    if (!eligible || attempts >= backoff.length) return null;
    final delay = backoff[attempts++];
    return ReconnectAttempt(epoch, rememberedId!, attempts, delay);
  }

  bool accepts(ReconnectAttempt attempt) =>
      eligible && attempt.epoch == epoch && attempt.remoteId == rememberedId;
}

/// Serializes durable writes and rolls back a verification cancelled during put.
/// This contains only the local adapter's remoteId, never a pairing credential.
class RememberedDeviceStore {
  RememberedDeviceStore({required this.read, required this.write});
  final String? Function() read;
  final Future<void> Function(String?) write;
  Future<void> _tail = Future<void>.value();

  Future<bool> saveVerified(String id, bool Function() isCurrent) {
    final operation = _tail.then((_) async {
      if (id.isEmpty || !isCurrent()) return false;
      final previous = read();
      await write(id);
      if (!isCurrent()) {
        await write(previous);
        return false;
      }
      return true;
    });
    _tail = operation.then<void>((_) {}, onError: (Object _, StackTrace __) {});
    return operation;
  }
}
