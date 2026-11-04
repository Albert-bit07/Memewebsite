import copy
import importlib.util
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'benchmarks/relevance.py'
spec = importlib.util.spec_from_file_location('relevance', SCRIPT)
relevance = importlib.util.module_from_spec(spec)
spec.loader.exec_module(relevance)


class RelevanceTests(unittest.TestCase):
    def setUp(self):
        self.dataset = {'schema_version': 1, 'dataset_id': 'synthetic-test', 'catalog_version': 'fixture-v1',
                        'catalog_ids': ['a', 'b', 'c', 'd'], 'training_popularity': {'a': 100, 'b': 2, 'c': 1},
                        'queries': [{'id': 'q1', 'excluded_ids': ['a'], 'relevant_ids': ['b']},
                                    {'id': 'q2', 'excluded_ids': ['b'], 'relevant_ids': ['c', 'd']}]}
        self.predictions = {'schema_version': 1, 'dataset_id': 'synthetic-test', 'catalog_version': 'fixture-v1',
                            'rankings': {'q1': ['c', 'b'], 'q2': ['d', 'c']}}

    def test_metrics_have_hand_calculated_denominators(self):
        report = relevance.evaluate(self.dataset, self.predictions, k=2)
        score = report['metrics']['submitted']
        self.assertEqual(score['precision'], .75)
        self.assertEqual(score['recall'], 1)
        self.assertAlmostEqual(score['ndcg'], (1 / math.log2(3) + 1) / 2)
        self.assertEqual(score['mrr'], .75)
        self.assertEqual(report['metrics']['popularity']['recall'], .75)

    def test_missing_and_short_rankings_do_not_inflate_scores(self):
        self.predictions['rankings'] = {'q1': ['b']}
        report = relevance.evaluate(self.dataset, self.predictions, k=2)
        self.assertEqual(report['missing_rankings'], 1)
        self.assertEqual(report['metrics']['submitted']['precision'], .25)
        self.assertEqual(report['metrics']['submitted']['recall'], .5)
        self.predictions['rankings'] = {}
        score = relevance.evaluate(self.dataset, self.predictions)['metrics']['submitted']
        self.assertEqual(score, dict(precision=0, recall=0, ndcg=0, mrr=0))

    def test_baselines_ignore_prediction_changes_and_input_order(self):
        first = relevance.evaluate(self.dataset, self.predictions, k=2, seed='fixed')
        self.dataset['catalog_ids'].reverse()
        self.dataset['queries'].reverse()
        self.predictions['rankings'] = {}
        second = relevance.evaluate(self.dataset, self.predictions, k=2, seed='fixed')
        for name in ('random', 'popularity'):
            self.assertEqual(first['metrics'][name], second['metrics'][name])

    def test_rejects_leakage_duplicates_unknown_ids_and_version_mismatch(self):
        for ranking in (['a'], ['unknown'], ['b', 'b'], 'b'):
            with self.subTest(ranking=ranking):
                bad = copy.deepcopy(self.predictions)
                bad['rankings']['q1'] = ranking
                with self.assertRaises(ValueError): relevance.evaluate(self.dataset, bad)
        for mutate in (
            lambda d: d['queries'][0].update(relevant_ids=['a']),
            lambda d: d['queries'][0].update(relevant_ids=[]),
            lambda d: d['queries'][1].update(id='q1'),
            lambda d: d.update(catalog_ids=['a', 'a']),
            lambda d: d.update(training_popularity={'b': True}),
            lambda d: d.update(catalog_version='other'),
            lambda d: d.update(schema_version=True),
            lambda d: d.update(queries=[]),
        ):
            bad = copy.deepcopy(self.dataset)
            mutate(bad)
            with self.assertRaises(ValueError): relevance.evaluate(bad, self.predictions)
        for k in (0, -1, True, 101):
            with self.assertRaises(ValueError): relevance.evaluate(self.dataset, self.predictions, k=k)

    def test_cli_is_reproducible_and_fails_without_echoing_bad_input(self):
        with tempfile.TemporaryDirectory() as directory:
            dataset, predictions = Path(directory) / 'dataset.json', Path(directory) / 'predictions.json'
            dataset.write_text(json.dumps(self.dataset))
            predictions.write_text(json.dumps(self.predictions))
            command = [sys.executable, str(SCRIPT), str(dataset), str(predictions), '--k', '2']
            first = subprocess.run(command, capture_output=True, text=True)
            second = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertEqual(first.stdout, second.stdout)
            self.assertEqual(len(json.loads(first.stdout)['input_sha256']['dataset']), 64)
            for invalid in ('{"private":"secret","private":"secret"}', 'private secret'):
                predictions.write_text(invalid)
                result = subprocess.run(command, capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, '')
                self.assertNotIn('private', result.stderr)


if __name__ == '__main__':
    unittest.main()
