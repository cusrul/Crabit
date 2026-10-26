import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

class WeatherLocation {
  const WeatherLocation({
    required this.name,
    required this.latitude,
    required this.longitude,
  });
  final String name;
  final double latitude, longitude;

  Map<String, Object> toJson() => {
        'name': name,
        'latitude': latitude,
        'longitude': longitude,
      };
  factory WeatherLocation.fromJson(Map value) {
    final name = value['name'];
    final lat = value['latitude'], lon = value['longitude'];
    if (name is! String ||
        name.trim().isEmpty ||
        lat is! num ||
        lon is! num ||
        !lat.isFinite ||
        !lon.isFinite ||
        lat < -90 ||
        lat > 90 ||
        lon < -180 ||
        lon > 180) {
      throw const FormatException('Invalid weather location');
    }
    return WeatherLocation(
      name: name,
      latitude: lat.toDouble(),
      longitude: lon.toDouble(),
    );
  }
}

class WeatherDay {
  const WeatherDay({
    required this.date,
    required this.code,
    required this.lowC,
    required this.highC,
    required this.rainProbability,
  });
  final DateTime date;
  final int code, rainProbability;
  final double lowC, highC;
  int get lowTenths => (lowC * 10).round();
  int get highTenths => (highC * 10).round();
  Map<String, Object> toJson() => {
        'date': _dateString(date),
        'code': code,
        'lowC': lowC,
        'highC': highC,
        'rainProbability': rainProbability,
      };
}

class WeatherForecast {
  const WeatherForecast({
    required this.timezone,
    required this.utcOffsetSeconds,
    required this.days,
  });
  final String timezone;
  final int utcOffsetSeconds;
  final List<WeatherDay> days;
  int get offsetMinutes => utcOffsetSeconds ~/ 60;
  Map<String, Object> toJson() => {
        'timezone': timezone,
        'utcOffsetSeconds': utcOffsetSeconds,
        'days': days.map((d) => d.toJson()).toList(),
      };

  factory WeatherForecast.parse(
    Map<String, dynamic> json, {
    required int requestedDays,
  }) {
    if (requestedDays < 1 || requestedDays > 7) {
      throw const FormatException('days outside firmware range');
    }
    final timezone = json['timezone'],
        offset = json['utc_offset_seconds'],
        daily = json['daily'];
    if (timezone is! String ||
        timezone.isEmpty ||
        offset is! int ||
        offset % 60 != 0 ||
        offset < -43200 ||
        offset > 50400 ||
        daily is! Map) {
      throw const FormatException('Invalid forecast metadata');
    }
    List list(String key) {
      final value = daily[key];
      if (value is! List || value.length != requestedDays) {
        throw FormatException('Invalid daily $key');
      }
      return value;
    }

    final dates = list('time');
    final codes = list('weather_code');
    final lows = list('temperature_2m_min');
    final highs = list('temperature_2m_max');
    final rain = list('precipitation_probability_max');
    final days = <WeatherDay>[];
    DateTime? previous;
    for (var i = 0; i < requestedDays; i++) {
      final rawCode = codes[i],
          rawLow = lows[i],
          rawHigh = highs[i],
          rawRain = rain[i];
      if (dates[i] is! String ||
          rawCode is! num ||
          rawLow is! num ||
          rawHigh is! num ||
          rawRain is! num) {
        throw const FormatException('Daily forecast values have invalid types');
      }
      final date = _parseDate(dates[i] as String);
      final code = rawCode, low = rawLow, high = rawHigh, probability = rawRain;
      if (date == null ||
          (previous != null && date.difference(previous).inDays != 1) ||
          code % 1 != 0 ||
          code < 0 ||
          code > 99 ||
          !low.isFinite ||
          !high.isFinite ||
          low < -100 ||
          high > 100 ||
          low > high ||
          low * 10 < -1000 ||
          high * 10 > 1000 ||
          probability % 1 != 0 ||
          probability < 0 ||
          probability > 100) {
        throw const FormatException('Forecast day outside firmware contract');
      }
      days.add(
        WeatherDay(
          date: date,
          code: code.toInt(),
          lowC: low.toDouble(),
          highC: high.toDouble(),
          rainProbability: probability.toInt(),
        ),
      );
      previous = date;
    }
    return WeatherForecast(
      timezone: timezone,
      utcOffsetSeconds: offset,
      days: List.unmodifiable(days),
    );
  }

  factory WeatherForecast.fromJson(Map value) {
    final timezone = value['timezone'],
        offset = value['utcOffsetSeconds'],
        rawDays = value['days'];
    if (timezone is! String ||
        offset is! int ||
        rawDays is! List ||
        rawDays.isEmpty ||
        rawDays.length > 7) {
      throw const FormatException('Invalid cached forecast');
    }
    final json = <String, dynamic>{
      'timezone': timezone,
      'utc_offset_seconds': offset,
      'daily': {
        'time': [],
        'weather_code': [],
        'temperature_2m_min': [],
        'temperature_2m_max': [],
        'precipitation_probability_max': [],
      },
    };
    for (final raw in rawDays) {
      if (raw is! Map) {
        throw const FormatException('Invalid cached day');
      }
      (json['daily']['time'] as List).add(raw['date']);
      (json['daily']['weather_code'] as List).add(raw['code']);
      (json['daily']['temperature_2m_min'] as List).add(raw['lowC']);
      (json['daily']['temperature_2m_max'] as List).add(raw['highC']);
      (json['daily']['precipitation_probability_max'] as List).add(
        raw['rainProbability'],
      );
    }
    return WeatherForecast.parse(json, requestedDays: rawDays.length);
  }
}

abstract interface class WeatherService {
  Future<List<WeatherLocation>> search(String query);
  Future<WeatherForecast> forecast(WeatherLocation location, {int days = 3});
  void close();
}

class OpenMeteoWeatherService implements WeatherService {
  OpenMeteoWeatherService({HttpClient? client})
      : _client = client ?? HttpClient(),
        _ownsClient = client == null;
  final HttpClient _client;
  final bool _ownsClient;

  @override
  void close() {
    if (_ownsClient) _client.close(force: true);
  }

  Future<Map<String, dynamic>> _get(Uri uri) async {
    final request =
        await _client.getUrl(uri).timeout(const Duration(seconds: 12));
    request.headers.set(HttpHeaders.acceptHeader, 'application/json');
    final response = await request.close().timeout(const Duration(seconds: 12));
    if (response.statusCode != HttpStatus.ok) {
      throw HttpException('Weather service HTTP ${response.statusCode}');
    }
    final bytes = BytesBuilder(copy: false);
    await for (final chunk in response.timeout(const Duration(seconds: 12))) {
      if (bytes.length + chunk.length > 128 * 1024) {
        throw const FormatException('Weather response exceeds 128 KiB limit');
      }
      bytes.add(chunk);
    }
    final body = utf8.decode(bytes.takeBytes());
    final value = jsonDecode(body);
    if (value is! Map<String, dynamic>) {
      throw const FormatException('Invalid weather response');
    }
    return value;
  }

  @override
  Future<List<WeatherLocation>> search(String query) async {
    final q = query.trim();
    if (q.length < 2) return const [];
    final json = await _get(
      Uri.https('geocoding-api.open-meteo.com', '/v1/search', {
        'name': q,
        'count': '8',
        'language': 'zh',
        'format': 'json',
      }),
    );
    final results = json['results'];
    if (results == null) return const [];
    if (results is! List) {
      throw const FormatException('Invalid geocoding results');
    }
    return results.whereType<Map>().map((r) {
      final parts = [
        r['name'],
        r['admin1'],
        r['country'],
      ].whereType<String>().where((s) => s.isNotEmpty).toSet();
      return WeatherLocation.fromJson({
        'name': parts.join(', '),
        'latitude': r['latitude'],
        'longitude': r['longitude'],
      });
    }).toList(growable: false);
  }

  @override
  Future<WeatherForecast> forecast(
    WeatherLocation location, {
    int days = 3,
  }) async {
    if (days < 1 || days > 7) {
      throw const FormatException('days outside firmware range');
    }
    final json = await _get(
      Uri.https('api.open-meteo.com', '/v1/forecast', {
        'latitude': location.latitude.toString(),
        'longitude': location.longitude.toString(),
        'daily':
            'weather_code,temperature_2m_min,temperature_2m_max,precipitation_probability_max',
        'forecast_days': '$days',
        'timezone': 'auto',
        'temperature_unit': 'celsius',
      }),
    );
    return WeatherForecast.parse(json, requestedDays: days);
  }
}

DateTime? _parseDate(String value) {
  final match = RegExp(r'^(\d{4})-(\d{2})-(\d{2})$').firstMatch(value);
  if (match == null) return null;
  final y = int.parse(match[1]!),
      m = int.parse(match[2]!),
      d = int.parse(match[3]!);
  if (y < 2024 || y > 2099) return null;
  final date = DateTime.utc(y, m, d);
  return date.year == y && date.month == m && date.day == d ? date : null;
}

String _dateString(DateTime date) =>
    '${date.year.toString().padLeft(4, '0')}${date.month.toString().padLeft(2, '0')}${date.day.toString().padLeft(2, '0')}';
