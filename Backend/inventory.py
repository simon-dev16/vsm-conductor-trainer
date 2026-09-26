"""Inventory mutations are applied by the server after world-interaction checks."""
from copy import deepcopy


class InventoryError(ValueError):
    pass


def empty_inventory():
    return [None] * 8


def acquire(slots, item_id, instance_id):
    if len(slots) != 8 or not item_id or not instance_id:
        raise InventoryError('Invalid inventory or item')
    if any(slot and slot['instance_id'] == instance_id for slot in slots):
        raise InventoryError('Item instance is already owned')
    if None not in slots:
        raise InventoryError('Inventory is full')
    result = deepcopy(slots)
    result[result.index(None)] = {'item_id': item_id, 'instance_id': instance_id}
    return result


def transfer(slots, slot_index, expected_item, expected_instance):
    if len(slots) != 8 or type(slot_index) is not int or not 0 <= slot_index < 8:
        raise InventoryError('Invalid slot')
    slot = slots[slot_index]
    if not slot or slot['item_id'] != expected_item or slot['instance_id'] != expected_instance:
        raise InventoryError('Item has changed or is not owned')
    result = deepcopy(slots)
    result[slot_index] = None
    return result
