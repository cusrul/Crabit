import 'package:flutter/material.dart';

void main() {
  runApp(const FirstPageApp());
}

class FirstPageApp extends StatelessWidget {
  const FirstPageApp({super.key});

  @override
  Widget build(BuildContext context) {
    return const MaterialApp(
      debugShowCheckedModeBanner: false,
      home: CounterPage(),
    );
  }
}

class CounterPage extends StatefulWidget {
  const CounterPage({super.key});

  @override
  State<CounterPage> createState() => _CounterPageState();
}

class _CounterPageState extends State<CounterPage> {
  int _value = 0;

  void _increase() {
    setState(() {
      _value += 1;
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('My first page')),
      body: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Text('Current value'),
            const SizedBox(height: 8),
            Text('Value: $_value', key: const ValueKey('value-label')),
            const SizedBox(height: 16),
            FilledButton(onPressed: _increase, child: const Text('Add one')),
          ],
        ),
      ),
    );
  }
}
