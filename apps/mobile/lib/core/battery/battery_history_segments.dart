import 'battery_sample.dart';

/// Splits chartable samples wherever the history contains an explicit or
/// inferred discontinuity. Invalid readings form gaps rather than zeroes.
List<List<BatterySample>> splitBatteryHistorySegments(
  Iterable<BatterySample> samples, {
  Duration maxGap = const Duration(minutes: 2),
}) {
  final ordered = samples.toList()..sort(_compareSamples);
  final segments = <List<BatterySample>>[];
  var segment = <BatterySample>[];
  BatterySample? previous;

  void closeSegment() {
    if (segment.isNotEmpty) {
      segments.add(List.unmodifiable(segment));
      segment = <BatterySample>[];
    }
    previous = null;
  }

  for (final sample in ordered) {
    if (sample.breakBefore) closeSegment();
    if (!sample.isValid) {
      closeSegment();
      continue;
    }
    if (previous != null) {
      final gap = sample.receivedAtUTC.difference(previous!.receivedAtUTC);
      if (gap > maxGap || isBatteryDeviceReset(previous!, sample)) {
        closeSegment();
      }
    }
    segment.add(sample);
    previous = sample;
  }
  closeSegment();
  return List.unmodifiable(segments);
}

int _compareSamples(BatterySample a, BatterySample b) =>
    a.receivedAtUTC.compareTo(b.receivedAtUTC);
