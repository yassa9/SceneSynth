import os
from ultralytics import YOLO
import cv2

def detect_and_save_image(image_path, model_path='yolov8l-cls.pt'):
    # Change directory to ../py_assets
    os.chdir('../py_assets')

    # Load the YOLOv8 model
    model = YOLO(model_path)

    # Perform detection
    results = model(image_path)

    # Parse results
    for result in results:
        img = result.plot()  # Render the output image

        # Define the output directory and create it if it doesn't exist
        output_dir = os.path.join(os.path.dirname(image_path), 'outputs')
        os.makedirs(output_dir, exist_ok=True)

        # Define the output path with the same name as the input image
        output_path = os.path.join(output_dir, os.path.basename(image_path))

        # Save the output image in the 'outputs' directory
        cv2.imwrite(output_path, img)

# Example usage
if __name__ == "__main__":
    image_path = '/home/yassa/repos/FlorenceFlame/py_assets/car.jpg'
    detect_and_save_image(image_path)
