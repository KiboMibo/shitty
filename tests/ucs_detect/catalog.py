import ast
from pathlib import Path


ROOT = Path(__file__).resolve().parent

TABLES = {
    "wide": ("table_wide.py", "WIDE_CHARACTERS", 2),
    "narrow": ("table_narrow.py", "NARROW_CHARACTERS", 1),
    "zwj": ("table_zwj.py", "EMOJI_ZWJ_SEQUENCES", 2),
    "vs15": ("table_vs15.py", "VS15_WIDE_TO_NARROW", 1),
    "vs16": ("table_vs16.py", "VS16_NARROW_TO_WIDE", 2),
    "ri": ("table_ri.py", "REGIONAL_INDICATOR_FLAGS", 2),
    "sri": ("table_sri.py", "STANDALONE_REGIONAL_INDICATORS", 2),
    "sfz": ("table_sfz.py", "STANDALONE_FITZPATRICK", 2),
}


def read_constant(filename, name):
    tree = ast.parse((ROOT / filename).read_text())
    for statement in tree.body:
        if not isinstance(statement, ast.Assign):
            continue
        if any(isinstance(target, ast.Name) and target.id == name
               for target in statement.targets):
            return ast.literal_eval(statement.value)
    raise RuntimeError(f"missing {name} in {filename}")


def sequence_text(value):
    if isinstance(value, int):
        return chr(value)
    return "".join(map(chr, value))


def category_cases(category):
    seen = set()
    if category == "lang":
        table = read_constant("table_lang.py", "LANG_GRAPHEMES")
        language_occurrences = {}
        for width, languages in table:
            for language, graphemes in languages:
                label = language.replace(" ", "_").replace("/", "_")
                occurrence_key = (width, label)
                occurrence = language_occurrences.get(occurrence_key, 0) + 1
                language_occurrences[occurrence_key] = occurrence
                if occurrence > 1:
                    label += f"_{occurrence}"
                for index, grapheme in enumerate(graphemes):
                    key = (width, grapheme)
                    if key in seen:
                        continue
                    seen.add(key)
                    yield (
                        f"lang/{width}/{label}/{index:04x}",
                        width,
                        grapheme.encode(),
                    )
        return

    base_only = category == "vs16_base"
    source_category = "vs16" if base_only else category
    filename, name, expected = TABLES[source_category]
    if base_only:
        expected = 1
    for version, entries in read_constant(filename, name):
        for index, value in enumerate(entries):
            if base_only:
                value = value[0]
            text = sequence_text(value)
            key = (expected, text)
            if key in seen:
                continue
            seen.add(key)
            yield (
                f"{category}/{version}/{index:06x}",
                expected,
                text.encode(),
            )


def all_categories():
    return (*TABLES, "vs16_base", "lang")
