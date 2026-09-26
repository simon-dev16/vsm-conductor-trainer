import unittest
from inventory import acquire, empty_inventory, transfer, InventoryError


class InventoryTests(unittest.TestCase):
    def test_eight_slots_full_and_duplicate_protection(self):
        slots = empty_inventory()
        for i in range(8):
            slots = acquire(slots, 'water', str(i))
        with self.assertRaises(InventoryError):
            acquire(slots, 'water', 'ninth')
        with self.assertRaises(InventoryError):
            acquire(slots, 'water', '0')
        self.assertEqual(len(slots), 8)

    def test_transfer_requires_owned_instance_and_does_not_mutate_input(self):
        slots = acquire(empty_inventory(), 'water', 'bottle-1')
        with self.assertRaises(InventoryError):
            transfer(slots, 0, 'water', 'bottle-2')
        result = transfer(slots, 0, 'water', 'bottle-1')
        self.assertIsNone(result[0])
        self.assertEqual(slots[0]['instance_id'], 'bottle-1')
        with self.assertRaises(InventoryError):
            transfer(result, 0, 'water', 'bottle-1')


if __name__ == '__main__':
    unittest.main()
