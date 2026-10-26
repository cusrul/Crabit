import 'dart:convert';

import 'package:crabit_watch_app/core/managers/ble_manager.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test(
    'device discovery matches the proven advertiser name case-insensitively',
    () {
      expect(isCrabitWatchName('Crabit-Watch'), isTrue);
      expect(isCrabitWatchName('CRABIT-WATCH'), isTrue);
      expect(isCrabitWatchName('Ameba ADV'), isFalse);
    },
  );

  test('only the exact pending probe echo can validate pair transfer', () {
    final expected = pairTransferProbeForRequestId('123456');

    expect(
        utf8.encode(expected).length, lessThanOrEqualTo(bleWriteChunkMaxBytes));

    expect(isMatchingPairTransferEcho(expected, expected), isTrue);
    expect(
      isMatchingPairTransferEcho(
        expected,
        pairTransferProbeForRequestId('654321'),
      ),
      isFalse,
    );
    expect(isMatchingPairTransferEcho(expected, 'PAIR_CONFIRMED'), isFalse);
    expect(isMatchingPairTransferEcho(null, expected), isFalse);
  });

  test('verified pair transfer retains its request identity and latency', () {
    const status = PairTransferStatus(
      stage: PairTransferStage.verified,
      requestId: '123456',
      latency: Duration(milliseconds: 128),
    );

    expect(status.canRetry, isTrue);
    expect(status.requestId, '123456');
    expect(status.latency?.inMilliseconds, 128);
  });
}
