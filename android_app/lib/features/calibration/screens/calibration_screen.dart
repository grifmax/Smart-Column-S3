import 'package:flutter/material.dart';

class CalibrationScreen extends StatelessWidget {
  const CalibrationScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Калибровка'),
      ),
      body: const Center(
        child: Text('Калибровка'),
      ),
    );
  }
}

