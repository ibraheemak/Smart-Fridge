import 'dart:convert';
import 'package:http/http.dart' as http;
import '../config.dart';
import '../models/fridge_item.dart';

class RecipeSuggestion {
  final String name;
  final String time;
  final String difficulty;
  final String description;
  final List<String> ingredients;

  const RecipeSuggestion({
    required this.name,
    required this.time,
    required this.difficulty,
    required this.description,
    required this.ingredients,
  });

  factory RecipeSuggestion.fromMap(Map<String, dynamic> m) => RecipeSuggestion(
        name: m['name'] as String? ?? '',
        time: m['time'] as String? ?? '',
        difficulty: m['difficulty'] as String? ?? 'medium',
        description: m['description'] as String? ?? '',
        ingredients: (m['ingredients'] as List?)?.cast<String>() ?? [],
      );
}

class GeminiService {
  /// Ask Gemini for 3 recipes based on current fridge contents.
  /// Uses the same API key and endpoint as the ESP32-CAM.
  static Future<List<RecipeSuggestion>> getRecipes(
      List<FridgeItem> items) async {
    if (AppConfig.geminiApiKey == 'YOUR_GEMINI_API_KEY') {
      throw Exception(
          'Gemini API key not set — edit lib/config.dart and add your key.');
    }

    final itemNames = items.map((i) => i.name).join(', ');

    // Same structure as the ESP32 uses, but different prompt
    final body = jsonEncode({
      'contents': [
        {
          'parts': [
            {
              'text': '''
Items currently in my fridge: $itemNames

Suggest 3 recipes I can make. Return ONLY valid JSON, no markdown, no explanation:
{
  "recipes": [
    {
      "name": "Recipe Name",
      "time": "25 min",
      "difficulty": "easy",
      "description": "One sentence description.",
      "ingredients": ["ingredient1", "ingredient2"]
    }
  ]
}
'''
            }
          ]
        }
      ]
    });

    final res = await http
        .post(
          Uri.parse(
              '${AppConfig.geminiEndpoint}?key=${AppConfig.geminiApiKey}'),
          headers: {'Content-Type': 'application/json'},
          body: body,
        )
        .timeout(const Duration(seconds: 30));

    if (res.statusCode != 200) {
      throw Exception('Gemini API error ${res.statusCode}: ${res.body}');
    }

    final raw = jsonDecode(res.body);
    final text = raw['candidates']?[0]?['content']?['parts']?[0]?['text']
            as String? ??
        '';

    // Strip markdown code fences the model sometimes adds
    final clean = text
        .replaceAll(RegExp(r'```json\s*'), '')
        .replaceAll(RegExp(r'```\s*'), '')
        .trim();

    final parsed = jsonDecode(clean) as Map<String, dynamic>;
    final list = parsed['recipes'] as List? ?? [];
    return list
        .map((r) => RecipeSuggestion.fromMap(r as Map<String, dynamic>))
        .toList();
  }
}
