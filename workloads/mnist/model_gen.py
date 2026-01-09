import torch
import torch.nn as nn
import torch.optim as optim
from torchvision import datasets, transforms
import numpy as np
import os
import random

# --- 1. REPRODUCIBILITY SETUP ---
def set_seed(seed=42):
    torch.manual_seed(seed)
    np.random.seed(seed)
    random.seed(seed)
    torch.backends.cudnn.deterministic = True
    torch.backends.cudnn.benchmark = False

set_seed(42)

# --- 2. SETTINGS & DIRECTORY ---
OUTPUT_DIR = "workloads/mnist/"
os.makedirs(OUTPUT_DIR, exist_ok=True)

# --- 3. DEFINE MODEL ---
class TinyMNIST(nn.Module):
    def __init__(self):
        super(TinyMNIST, self).__init__()
        self.fc1 = nn.Linear(784, 64, bias=True) 
        self.fc2 = nn.Linear(64, 10, bias=True)

    def forward(self, x):
        x = x.view(-1, 784)
        x = torch.relu(self.fc1(x))
        x = self.fc2(x)
        return x

# --- 4. TRAIN MODEL (DETERMINISTIC) ---
print("Training model...")
transform = transforms.Compose([transforms.ToTensor(), transforms.Normalize((0.5,), (0.5,))])
# shuffle=False is critical so train_set[0] is always the same image
train_set = datasets.MNIST('./data', train=True, download=True, transform=transform)
train_loader = torch.utils.data.DataLoader(train_set, batch_size=64, shuffle=False)

model = TinyMNIST()
optimizer = optim.SGD(model.parameters(), lr=0.1)
criterion = nn.CrossEntropyLoss()

for batch_idx, (data, target) in enumerate(train_loader):
    optimizer.zero_grad()
    output = model(data)
    loss = criterion(output, target)
    loss.backward()
    optimizer.step()
    if batch_idx >= 200: break

# --- 5. EXPORT HELPERS ---
def quantize(data):
    scale = np.abs(data).max()
    if scale == 0: scale = 1
    return (data / scale * 127).astype(np.int8)

def write_header(filename, name, data_int8):
    full_path = os.path.join(OUTPUT_DIR, filename)
    with open(full_path, "w") as f:
        f.write(f"#ifndef {name.upper()}_H\n#define {name.upper()}_H\n\n#include <stdint.h>\n\n")
        f.write(f"const int8_t {name}[] = {{\n")
        f.write(", ".join(map(str, data_int8.flatten())))
        f.write("\n};\n#endif\n")

# --- 6. DATA PREPARATION & EXPORT ---
w1_q = quantize(model.fc1.weight.detach().numpy())
b1_q = quantize(model.fc1.bias.detach().numpy())
w2_q = quantize(model.fc2.weight.detach().numpy())
b2_q = quantize(model.fc2.bias.detach().numpy())

sample_img, sample_label = train_set[0]
img_q = quantize(sample_img.numpy().flatten())

print(f"Exporting headers to {OUTPUT_DIR}...")
write_header("weights_l1.h", "w_l1", w1_q)
write_header("bias_l1.h", "b_l1", b1_q)
write_header("weights_l2.h", "w_l2", w2_q)
write_header("bias_l2.h", "b_l2", b2_q)
write_header("input_image.h", "img_input", img_q)

# --- 7. DETAILED DEBUG INFERENCE (MATCHES gem5) ---
print("\n" + "="*60)
print("PYTHON DEBUG VALUES (COMPARE TO gem5 OUTPUT)")
print("="*60)

# --- LAYER 1 ---
# Using int32 for the dot product to match hardware accumulation
l1_sum = np.dot(w1_q.astype(np.int32), img_q.astype(np.int32)) + b1_q.astype(np.int32)

print(f"\n[DEBUG] Layer 1 Raw Sums (First 10):")
print(l1_sum[:10])

# ReLU + Clamp to int8
l1_out = np.clip(np.maximum(0, l1_sum), 0, 127).astype(np.int8)

print(f"\n[DEBUG] Layer 1 Post-Clamp / next_in (First 10):")
print(l1_out[:10])

# --- LAYER 2 ---
l2_sum = np.dot(w2_q.astype(np.int32), l1_out.astype(np.int32)) + b2_q.astype(np.int32)

print(f"\n[DEBUG] Layer 2 Raw Sums (Final Scores - First 10):")
print(l2_sum[:10])

# --- FINAL PREDICTION ---
prediction = np.argmax(l2_sum)
print("\n" + "="*60)
print(f"TARGET LABEL:    {sample_label}")
print(f"PYTHON PREDICT:  {prediction}")
print("="*60)