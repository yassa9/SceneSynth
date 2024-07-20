import os
from transformers import AutoImageProcessor, AutoModelForDepthEstimation
import torch
import numpy as np
from PIL import Image

def estimate_and_save_depth(image_path, model_name='depth-anything/Depth-Anything-V2-Small-hf'):
    # Load the image
    image = Image.open(image_path)

    # Load the model and processor
    image_processor = AutoImageProcessor.from_pretrained(model_name)
    model = AutoModelForDepthEstimation.from_pretrained(model_name)

    # Prepare image for the model
    inputs = image_processor(images=image, return_tensors="pt")

    # Perform depth estimation
    with torch.no_grad():
        outputs = model(**inputs)
        predicted_depth = outputs.predicted_depth

    # Interpolate to original size
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

    # Define the output directory and create it if it doesn't exist
    output_dir = os.path.join(os.path.dirname(image_path), 'outputs')
    os.makedirs(output_dir, exist_ok=True)

    # Define the output path with the same name as the input image
    output_path = os.path.join(output_dir, os.path.basename(image_path))

    # Save the output image in the 'outputs' directory
    depth.save(output_path)

# Example usage
if __name__ == "__main__":
    image_path = '/home/yassa/repos/FlorenceFlame/py_assets/car.jpg'
    estimate_and_save_depth(image_path)
