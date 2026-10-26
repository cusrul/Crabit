class UsbScript {
  const UsbScript({required this.id, required this.title, required this.body});

  final String id;
  final String title;
  final String body;

  @override
  bool operator ==(Object other) =>
      other is UsbScript &&
      other.id == id &&
      other.title == title &&
      other.body == body;

  @override
  int get hashCode => Object.hash(id, title, body);

  Map<String, String> toMap() => {'id': id, 'title': title, 'body': body};

  static UsbScript? fromMap(Map<dynamic, dynamic> value) {
    final id = value['id'];
    final title = value['title'];
    final body = value['body'];
    if (id is! String || title is! String || body is! String) return null;
    return UsbScript(id: id, title: title, body: body);
  }
}
