export interface HelloWorldModule {
  new (): HelloWorldInstance
}

export interface HelloWorldInstance {
  sayHello(): string
}

import { createRequire } from "node:module"

const require = createRequire(import.meta.url)
const helloWorldModule = require("../../build/Release/hello_world.node")

// Import the addon using dynamic import (for ESM compatibility)
async function runExample() {
  try {
    const { HelloWorld } = helloWorldModule as { HelloWorld: HelloWorldModule }

    const helloWorldInstance = new HelloWorld()
    const result = helloWorldInstance.sayHello()

    console.log(result) // Should output: Hello World
  } catch (error) {
    console.error("Error loading the addon:", error)
  }
}

runExample().catch(console.error)
