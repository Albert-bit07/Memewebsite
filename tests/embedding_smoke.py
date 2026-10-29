"""Real CPU dependency smoke test, using random weights and generated pixels.

No pretrained download or recommendation-quality claim is made by this test.
Run explicitly in the optional backend/python environment, not the native job.
"""
import json
from pathlib import Path
import sys
import tempfile
from unittest.mock import patch

import clip
from clip.model import CLIP
import numpy as np
from PIL import Image
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "backend/python"))
import py_recommender
from catalog import write_catalog


def main():
    torch.manual_seed(7)
    torch.set_num_threads(1)
    model = CLIP(
        embed_dim=8, image_resolution=32, vision_layers=1, vision_width=64,
        vision_patch_size=16, context_length=8, vocab_size=32,
        transformer_width=64, transformer_heads=1, transformer_layers=1,
    ).eval()
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        checkpoint = root / "random-weights.pt"
        # Official CLIP downloads are TorchScript archives. Exercise that format;
        # the upstream plain-state-dict fallback does not rewind its input file.
        traced = torch.jit.trace_module(model, {
            "forward": (torch.zeros(1, 3, 32, 32), torch.zeros(1, 8, dtype=torch.long)),
            "encode_image": (torch.zeros(1, 3, 32, 32),),
        })
        torch.jit.save(traced, str(checkpoint))
        with patch("urllib.request.urlopen", side_effect=AssertionError("Unexpected download")):
            loaded, preprocess = clip.load(str(checkpoint), device="cpu", jit=False)
            py_recommender._model = loaded.eval()
            py_recommender._preprocess = preprocess
            py_recommender._device = "cpu"
            images = root / "images"
            images.mkdir()
            for name, mode, color in [("a.png", "L", 100), ("b.png", "RGBA", (20, 60, 90, 128))]:
                with Image.new(mode, (48, 40), color) as image:
                    image.save(images / name)
            paths, vectors = py_recommender.embed_folder(images)
            values = np.asarray(vectors)
            assert values.shape == (2, 8), values.shape
            assert np.isfinite(values).all()
            np.testing.assert_allclose(np.linalg.norm(values, axis=1), 1, atol=1e-5)
            np.testing.assert_allclose(py_recommender.embed_image(paths[0]), values[0], atol=1e-6)
            assert clip.tokenize(["dependency smoke test"]).shape == (1, 77)
            output = root / "catalog"
            write_catalog(output, paths, vectors, "random-smoke-only", "smoke-v1")
            manifest = json.loads((output / "catalog.json").read_text())
            assert manifest["dimension"] == 8
            assert len(manifest["items"]) == 2
            for item in manifest["items"]:
                assert (output / item["path"]).is_file()
    print("CPU CLIP load, Pillow/torchvision preprocessing, inference, NumPy and catalog smoke passed")


if __name__ == "__main__":
    main()
