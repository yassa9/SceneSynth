from transformers import AutoImageProcessor, GLPNForDepthEstimation
import torch
import numpy as np
from PIL import Image
import requests
import matplotlib.pyplot as plt

# Load the image
image_path = "car.jpg"  # Replace with your image path
image = Image.open(image_path)

# Load the model and processor
image_processor = AutoImageProcessor.from_pretrained("vinvino02/glpn-kitti")
model = GLPNForDepthEstimation.from_pretrained("vinvino02/glpn-kitti")

# Prepare image for the model
inputs = image_processor(images=image, return_tensors="pt")

# Perform depth estimation
with torch.no_grad():
    outputs = model(**inputs)
    predicted_depth = outputs.predicted_depth

# Interpolate to the original size
prediction = torch.nn.functional.interpolate(
    predicted_depth.unsqueeze(1),
    size=image.size[::-1],
    mode="bicubic",
    align_corners=False,
)

# Visualize the prediction
output = prediction.squeeze().cpu().numpy()
formatted = (output * 255 / np.max(output)).astype("uint8")
depth = Image.fromarray(formatted)

# Plot the original image and the depth map
fig, ax = plt.subplots(1, 2, figsize=(12, 6))

# Original image
ax[0].imshow(image)
ax[0].set_title('Original Image')
ax[0].axis('off')

# Depth map
ax[1].imshow(depth, cmap='inferno')
ax[1].set_title('Depth Map')
ax[1].axis('off')

plt.show()
