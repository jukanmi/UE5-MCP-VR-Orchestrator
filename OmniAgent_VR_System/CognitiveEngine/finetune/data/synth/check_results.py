import yaml
import collections

data = yaml.safe_load(open('generated/scenarios.yaml', encoding='utf-8'))
ac = collections.Counter()
cats = collections.Counter()
multi_actions_count = 0

for s in data:
    acts = s['gold']['actions']
    if acts:
        ac[acts[0]['type']] += 1
    else:
        ac['(no-action)'] += 1
    cats[s['category']] += 1
    if len(acts) > 1:
        multi_actions_count += 1

print(f'Total scenarios: {len(data)}')
print(f'Unique utterances: {len({s["utterance"] for s in data})}')
print(f'Unique personas: {len({s["npc"] for s in data})}')
print(f'Multi-action scenarios: {multi_actions_count}')
print()
print('[Category distribution]')
for k, v in cats.most_common():
    print(f'  {k}: {v}')
print()
print('[Action distribution Top 10]')
for k, v in ac.most_common(10):
    print(f'  {k}: {v}')
