import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'backend/python'))
from catalog import write_catalog

class CatalogTests(unittest.TestCase):
    def test_complete_artifact_and_stable_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = root / 'image.jpg'
            image.write_bytes(b'synthetic fixture')
            for version in ['one', 'two']:
                write_catalog(root / version, [image], [[1, 0]], 'synthetic', version)
            first = json.loads((root / 'one/catalog.json').read_text())
            second = json.loads((root / 'two/catalog.json').read_text())
            self.assertEqual(first['items'], second['items'])
            self.assertTrue((root / 'one' / first['items'][0]['path']).is_file())
            with self.assertRaises(FileExistsError):
                write_catalog(root / 'one', [image], [[1, 0]], 'synthetic', 'one')

    def test_invalid_generation_never_publishes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for vectors in [[], [[float('nan')]], [[]]]:
                with self.assertRaises(ValueError):
                    write_catalog(root / 'bad', ['missing.jpg'], vectors, 'synthetic', 'bad')
                self.assertFalse((root / 'bad').exists())
            with self.assertRaises(FileNotFoundError):
                write_catalog(root / 'bad', ['missing.jpg'], [[1]], 'synthetic', 'bad')
            self.assertFalse((root / 'bad').exists())
            self.assertFalse(list(root.glob('.catalog-*')))
