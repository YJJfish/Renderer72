#include "Engine.hpp"
#include "Scene72.hpp"

void Engine::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
	Engine* engine = reinterpret_cast<Engine*>(glfwGetWindowUserPointer(window));
	engine->framebufferResized = true;
}


void Engine::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
	Engine* engine = reinterpret_cast<Engine*>(glfwGetWindowUserPointer(window));
	auto now = std::chrono::steady_clock::now();
	if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
		if (engine->cameraMode == Engine::CameraMode::USER || engine->cameraMode == Engine::CameraMode::DEBUG) {
			if ((now - engine->mouseButtonLeftPressTime).count() <= 200000000) {
				engine->sceneViewer.reset();
				engine->sceneViewer.setZoomRate(10.0f);
			}
		}
		engine->mouseButtonLeftPressTime = now;
	}
}


void Engine::cursorPosCallback(GLFWwindow* window, double xPos, double yPos) {
	Engine* engine = reinterpret_cast<Engine*>(glfwGetWindowUserPointer(window));
	if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
	}
	else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
	}
	else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) {
		if (engine->cameraMode == Engine::CameraMode::USER || engine->cameraMode == Engine::CameraMode::DEBUG) {
			if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
				engine->sceneViewer.moveUp(static_cast<float>(0.001 * (yPos - engine->cursorY)));
				engine->sceneViewer.moveLeft(static_cast<float>(0.001 * (xPos - engine->cursorX)));
			}
			else if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS) {
				engine->sceneViewer.zoomIn(static_cast<float>(1.0 - 0.005 * (yPos - engine->cursorY)));
			}
			else {
				engine->sceneViewer.turn(static_cast<float>(0.002 * (engine->cursorX - xPos)), static_cast<float>(0.002 * (engine->cursorY - yPos)), 0.0f);
			}
		}
	}
	engine->cursorX = xPos;
	engine->cursorY = yPos;
}


void Engine::scrollCallback(GLFWwindow* window, double xOffset, double yOffset) {
	Engine* engine = reinterpret_cast<Engine*>(glfwGetWindowUserPointer(window));
	if (engine->cameraMode == Engine::CameraMode::USER || engine->cameraMode == Engine::CameraMode::DEBUG) {
		if (yOffset < 0.0)
			engine->sceneViewer.zoomOut(1.2f);
		else
			engine->sceneViewer.zoomIn(1.2f);
	}
}


void Engine::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	Engine* engine = reinterpret_cast<Engine*>(glfwGetWindowUserPointer(window));
	if (key == GLFW_KEY_SPACE) {
		if (action == GLFW_PRESS) {
			engine->switchPauseState();
		}
	}
	else if (key == GLFW_KEY_1) {
		if (action == GLFW_PRESS) {
			if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) {
				std::cout << "Set camera mode to USER mode." << std::endl;
				engine->setCameraMode(CameraMode::USER);
			}
		}
	}
	else if (key == GLFW_KEY_2) {
		if (action == GLFW_PRESS) {
			if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) {
				std::cout << "Set camera mode to SCENE mode." << std::endl;
				std::cout << "Available scene cameras:" << std::endl;
				for (const auto& camera : engine->pScene72->cameras)
					std::cout << camera.second->name << std::endl;
				std::cout << "Input camera name: ";
				std::string cameraName;
				std::cin >> cameraName;
				engine->setCameraMode(CameraMode::SCENE, cameraName);
			}
		}
	}
	else if (key == GLFW_KEY_3) {
		if (action == GLFW_PRESS) {
			if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) {
				std::cout << "Set camera mode to DEBUG mode." << std::endl;
				std::cout << "Available user cameras:" << std::endl;
				for (const auto& camera : engine->pScene72->cameras)
					std::cout << camera.second->name << std::endl;
				std::cout << "Input camera name: ";
				std::string cameraName;
				std::cin >> cameraName;
				engine->setCameraMode(CameraMode::DEBUG, cameraName);
			}
		}
	}
}