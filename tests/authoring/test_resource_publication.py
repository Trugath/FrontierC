"""Concurrent resource consumers must only observe complete JSON documents."""
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import resource_sources


class ResourcePublication(unittest.TestCase):
    def test_concurrent_compile_and_read(self):
        source = resource_sources.ROOT / 'data/intro/models.ini'
        expected = resource_sources.document('intro_models', source)

        def consume(_):
            for _ in range(12):
                path = resource_sources.compile_resource('intro_models', source)
                self.assertEqual(json.loads(resource_sources.read_published(path)), expected)

        with tempfile.TemporaryDirectory(prefix='fe2-resource-publication-') as directory:
            with patch.object(resource_sources, 'INTERMEDIATE', Path(directory)):
                with ThreadPoolExecutor(max_workers=12) as executor:
                    list(executor.map(consume, range(12)))
            self.assertFalse(list(Path(directory).rglob('*.tmp')))


if __name__ == '__main__':
    unittest.main()
