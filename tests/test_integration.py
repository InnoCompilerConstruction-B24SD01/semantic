"""Exercise the complete lexer -> parser -> semantic analyzer CLI pipeline."""

from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest


BINARY = Path(sys.argv.pop(1) if len(sys.argv) > 1 else "./dsemantic").resolve()
TESTS = Path(__file__).resolve().parent
PROJECT_EXAMPLES = TESTS.parents[1] / "project-d-examples" / "project_d_example_programs.md"
EXAMPLES = TESTS.parent / "examples"
CODE_BLOCK = re.compile(
    r"^(?P<indent> *)```text\n(?P<code>.*?)^(?P=indent)```[ \t]*$",
    re.MULTILINE | re.DOTALL,
)
TREE_LINE = re.compile(
    r'(?:  )*[A-Za-z]+(?: "(?:[^"\\]|\\.)*")?'
    r'(?: \[type=[a-z]+\])?(?: \[symbol=#[1-9][0-9]*\])?'
)


def supplied_programs():
    document = PROJECT_EXAMPLES.read_text(encoding="utf-8")
    headings = list(re.finditer(r"^## ([0-9]+)\. [^\n]+$", document, re.MULTILINE))
    if [int(heading[1]) for heading in headings] != list(range(1, 16)):
        raise ValueError("Expected exactly the 15 numbered Project D programs")
    programs = {}
    for index, heading in enumerate(headings):
        end = headings[index + 1].start() if index + 1 < len(headings) else len(document)
        blocks = list(CODE_BLOCK.finditer(document[heading.end():end]))
        if len(blocks) < 2:
            raise ValueError("Each numbered program must have source and expected output")
        programs["example_{:02d}".format(int(heading[1]))] = blocks[0]["code"]
    return programs


PROGRAMS = supplied_programs()


class SemanticCliTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="dlang semantic tests ")
        self.addCleanup(self.directory.cleanup)
        self.source_path = Path(self.directory.name) / "source file.d"

    def run_cli(self, *arguments):
        return subprocess.run([str(BINARY), *map(str, arguments)], capture_output=True, timeout=10)

    def run_source(self, source):
        self.source_path.write_bytes(source.encode("utf-8") if isinstance(source, str) else source)
        return self.run_cli(self.source_path)

    def check_valid(self, source):
        result = self.run_source(source)
        self.assertEqual(result.returncode, 0, result.stderr.decode("utf-8"))
        self.assertEqual(result.stderr, b"")
        lines = result.stdout.decode("utf-8").splitlines()
        self.assertTrue(lines)
        self.assertEqual(lines[0], "Program")
        for line in lines:
            self.assertIsNotNone(TREE_LINE.fullmatch(line), "Invalid AAST line: " + line)
        return result.stdout.decode("utf-8")

    def test_all_fifteen_supplied_programs(self):
        self.assertEqual(len(PROGRAMS), 15)
        for name, source in PROGRAMS.items():
            with self.subTest(name=name):
                self.check_valid(source)

    def test_checked_in_golden_examples(self):
        sources = sorted(EXAMPLES.glob("*.d"))
        self.assertGreaterEqual(len(sources), 2)
        for source in sources:
            if source.stem.startswith("invalid_"):
                continue
            with self.subTest(source=source.name):
                expected = source.with_suffix(".sem").read_text(encoding="utf-8")
                result = self.run_cli(source)
                self.assertEqual(result.returncode, 0, result.stderr.decode("utf-8"))
                self.assertEqual(result.stdout.decode("utf-8"), expected)

    def test_name_and_context_errors_have_positions_and_no_tree(self):
        cases = [
            ("print missing\n", "1:7", "undeclared identifier 'missing'"),
            ("var x\nvar x\n", "2:5", "duplicate declaration"),
            ("exit\n", "1:1", "only inside a loop"),
            ("return 1\n", "1:1", "only inside a function"),
            ("var t := {x := 1, x := 2}\n", "1:19", "duplicate tuple element"),
        ]
        for source, position, message in cases:
            with self.subTest(source=source):
                result = self.run_source(source)
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, b"")
                diagnostic = result.stderr.decode("utf-8")
                self.assertIn(str(self.source_path), diagnostic)
                self.assertIn("semantic error at " + position, diagnostic)
                self.assertIn(message, diagnostic)

    def test_known_type_errors_and_dynamic_deferral(self):
        invalid = self.run_source("print 1 + 'text'\n")
        self.assertEqual(invalid.returncode, 1)
        self.assertEqual(invalid.stdout, b"")
        self.assertIn(b"semantic error at 1:9", invalid.stderr)
        self.assertIn(b"int and string", invalid.stderr)

        output = self.check_valid("var x := 1\nvar y\nx := 'text'\nprint x + y\n")
        self.assertIn('BinaryExpression "+" [type=dynamic]', output)

    def test_lexical_and_syntax_errors_are_preserved(self):
        lexical = self.run_source("print @\n")
        self.assertEqual(lexical.returncode, 1)
        self.assertEqual(lexical.stdout, b"")
        self.assertIn(b"lexical error at 1:7", lexical.stderr)

        syntax = self.run_source("var x :=\n")
        self.assertEqual(syntax.returncode, 1)
        self.assertEqual(syntax.stdout, b"")
        self.assertIn(b"syntax error at 1:9", syntax.stderr)

    def test_missing_file_directory_and_arguments(self):
        missing = self.run_cli(self.source_path)
        self.assertEqual(missing.returncode, 1)
        self.assertIn(b"cannot open source file", missing.stderr)

        directory = self.run_cli(self.directory.name)
        self.assertEqual(directory.returncode, 1)
        self.assertIn(b"source file", directory.stderr)

        for arguments in [(), ("first.d", "second.d")]:
            with self.subTest(arguments=arguments):
                result = self.run_cli(*arguments)
                self.assertEqual(result.returncode, 2)
                self.assertIn(b"Usage:", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
