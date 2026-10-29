"""Controlled dependency-boundary checks; these do not run CLIP inference."""
import importlib.util
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import MagicMock, patch


class EmbeddingResourceTests(unittest.TestCase):
    def setUp(self):
        self.torch = MagicMock()
        self.clip = MagicMock()
        self.image_api = MagicMock()
        pil = types.ModuleType('PIL')
        pil.Image = self.image_api
        modules = {'torch': self.torch, 'clip': self.clip, 'PIL': pil,
                   'numpy': types.ModuleType('numpy')}
        path = Path(__file__).resolve().parents[1] / 'backend/python/py_recommender.py'
        spec = importlib.util.spec_from_file_location('embedding_under_test', path)
        self.embedding = importlib.util.module_from_spec(spec)
        with patch.dict(sys.modules, modules):
            spec.loader.exec_module(self.embedding)
        self.source = MagicMock()
        self.rgb = MagicMock()
        self.image_api.open.return_value = self.source
        self.source.__enter__.return_value = self.source
        self.source.convert.return_value = self.rgb
        self.rgb.__enter__.return_value = self.rgb
        self.embedding._model = MagicMock()
        self.embedding._preprocess = MagicMock()
        self.embedding._device = 'cpu'

    def test_images_close_on_success_and_each_failure_boundary(self):
        for failure in (None, 'convert', 'preprocess', 'encode'):
            with self.subTest(failure=failure):
                self.setUp()
                if failure == 'convert':
                    self.source.convert.side_effect = RuntimeError('conversion failed')
                elif failure == 'preprocess':
                    self.embedding._preprocess.side_effect = RuntimeError('preprocessing failed')
                elif failure == 'encode':
                    self.embedding._model.encode_image.side_effect = RuntimeError('encoding failed')
                if failure:
                    with self.assertRaises(RuntimeError):
                        self.embedding.embed_image('fixture.png')
                else:
                    self.embedding.embed_image('fixture.png')
                self.source.__exit__.assert_called_once()
                if failure != 'convert':
                    self.rgb.__exit__.assert_called_once()

    def test_failed_reload_preserves_previous_model_preprocessor_and_device(self):
        for stage in ('load', 'eval'):
            with self.subTest(stage=stage):
                self.setUp()
                original = (self.embedding._model, self.embedding._preprocess, self.embedding._device)
                candidate, preprocess = MagicMock(), MagicMock()
                self.clip.load.return_value = (candidate, preprocess)
                if stage == 'load':
                    self.clip.load.side_effect = RuntimeError('load failed')
                else:
                    candidate.eval.side_effect = RuntimeError('eval failed')
                with self.assertRaises(RuntimeError):
                    self.embedding.init_model('cuda')
                self.assertEqual((self.embedding._model, self.embedding._preprocess, self.embedding._device), original)

    def test_successful_reload_publishes_complete_state(self):
        candidate, preprocess = MagicMock(), MagicMock()
        self.clip.load.return_value = (candidate, preprocess)
        self.assertTrue(self.embedding.init_model('cpu'))
        candidate.eval.assert_called_once()
        self.assertIs(self.embedding._model, candidate)
        self.assertIs(self.embedding._preprocess, preprocess)
        self.assertEqual(self.embedding._device, 'cpu')


if __name__ == '__main__':
    unittest.main()
