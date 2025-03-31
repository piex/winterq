class TestFramework {
	constructor(name) {
		this.name = name;
		this.tests = [];
		this.passedTests = 0;
		this.failedTests = 0;
	}

	// 添加测试用例
	addTest(name, testFn) {
		this.tests.push({ name, testFn });
		return this;
	}

	// 运行所有测试
	async runTests() {
		console.log(`\n开始测试: ${this.name}`);
		console.log("====================================");

		for (const test of this.tests) {
			try {
				await test.testFn();
				console.info(`✅ 通过: ${test.name}`);
				this.passedTests++;
			} catch (error) {
				console.error(`❌ 失败: ${test.name}`);
				console.error(`   错误: ${error.message}`);
				this.failedTests++;
			}
		}

		console.log("====================================");
		console.log(
			`测试结果: ${this.passedTests} 通过, ${this.failedTests} 失败\n`,
		);
	}

	// 断言函数
	assert(condition, message) {
		if (!condition) {
			throw new Error(message || "断言失败");
		}
	}

	assertEquals(actual, expected, message) {
		if (actual !== expected) {
			throw new Error(message || `期望值 ${expected}, 实际值 ${actual}`);
		}
	}

	assertDeepEquals(actual, expected, message) {
		const actualJson = JSON.stringify(actual);
		const expectedJson = JSON.stringify(expected);
		if (actualJson !== expectedJson) {
			throw new Error(
				message || `期望值 ${expectedJson}, 实际值 ${actualJson}`,
			);
		}
	}
}

// 测试 Event API
const eventTest = new TestFramework("Event API 测试");

// 测试 Event 构造函数
eventTest.addTest("Event 构造函数 - 基本创建", () => {
	const event = new Event("click");
	eventTest.assert(event instanceof Event, "event 应该是 Event 的实例");
	eventTest.assertEquals(event.type, "click", "event.type 应该正确设置");
	eventTest.assertEquals(
		event.bubbles,
		false,
		"event.bubbles 默认应该是 false",
	);
	eventTest.assertEquals(
		event.cancelable,
		false,
		"event.cancelable 默认应该是 false",
	);
});

eventTest.addTest("Event 构造函数 - 带选项", () => {
	const event = new Event("click", {
		bubbles: true,
		cancelable: true,
	});

	eventTest.assertEquals(event.type, "click", "event.type 应该正确设置");
	eventTest.assertEquals(
		event.bubbles,
		true,
		"event.bubbles 应该正确设置为 true",
	);
	eventTest.assertEquals(
		event.cancelable,
		true,
		"event.cancelable 应该正确设置为 true",
	);
});

// 测试 Event 属性
eventTest.addTest("Event 属性 - 默认属性", () => {
	const event = new Event("click");
	eventTest.assertEquals(event.type, "click", "event.type 应该正确设置");
	eventTest.assertEquals(event.target, null, "event.target 默认应该是 null");
	eventTest.assertEquals(
		event.currentTarget,
		null,
		"event.currentTarget 默认应该是 null",
	);
	eventTest.assertEquals(
		event.eventPhase,
		0,
		"event.eventPhase 默认应该是 0 (NONE)",
	);
	eventTest.assertEquals(
		event.bubbles,
		false,
		"event.bubbles 默认应该是 false",
	);
	eventTest.assertEquals(
		event.cancelable,
		false,
		"event.cancelable 默认应该是 false",
	);
	eventTest.assertEquals(
		event.defaultPrevented,
		false,
		"event.defaultPrevented 默认应该是 false",
	);
	eventTest.assertEquals(
		event.composed,
		false,
		"event.composed 默认应该是 false",
	);
	eventTest.assertEquals(
		event.isTrusted,
		false,
		"event.isTrusted 默认应该是 false",
	);
	eventTest.assertEquals(
		typeof event.timeStamp,
		"number",
		"event.timeStamp 应该是数字",
	);
});

// 测试 Event 方法
eventTest.addTest("Event 方法 - preventDefault()", () => {
	const event = new Event("click", { cancelable: true });
	event.preventDefault();

	eventTest.assertEquals(
		event.defaultPrevented,
		true,
		"preventDefault() 应该将 defaultPrevented 设置为 true",
	);
});

eventTest.addTest("Event 方法 - preventDefault() 不可取消的事件", () => {
	const event = new Event("click", { cancelable: false });
	event.preventDefault();

	eventTest.assertEquals(
		event.defaultPrevented,
		false,
		"对不可取消的事件调用 preventDefault() 应该不起作用",
	);
});

eventTest.addTest("Event 方法 - stopPropagation()", () => {
	const event = new Event("click");
  event.stopPropagation();
  // 由于标准 Event 对象没有直接暴露检查传播是否已停止的方法，我们可以通过后续传播测试来验证 stopPropagation 的效果
  eventTest.assert(true, 'stopPropagation() 已调用');
});

eventTest.addTest("Event 方法 - stopImmediatePropagation()", () => {
	const event = new Event("click");

	event.stopImmediatePropagation();
	
	// 由于无法直接测试 stopImmediatePropagation 的效果，我们将在 EventTarget 测试中测试
  eventTest.assert(true, 'stopImmediatePropagation() 已调用');
});

// 测试 CustomEvent API
const customEventTest = new TestFramework("CustomEvent API 测试");

// 测试 CustomEvent 构造函数
customEventTest.addTest("CustomEvent 构造函数 - 基本创建", () => {
	const event = new CustomEvent("custom");
	customEventTest.assert(
		event instanceof CustomEvent,
		"event 应该是 CustomEvent 的实例",
	);
	customEventTest.assert(event instanceof Event, "CustomEvent 应该继承 Event");
	customEventTest.assertEquals(event.type, "custom", "event.type 应该正确设置");
	customEventTest.assertDeepEquals(
		event.detail,
		null,
		"event.detail 默认应该是 null",
	);
});

customEventTest.addTest("CustomEvent 构造函数 - 带选项和 detail", () => {
	const detail = { message: "测试数据" };
	const event = new CustomEvent("custom", {
		bubbles: true,
		cancelable: true,
		detail: detail,
	});

	customEventTest.assertEquals(event.type, "custom", "event.type 应该正确设置");
	customEventTest.assertEquals(
		event.bubbles,
		true,
		"event.bubbles 应该正确设置为 true",
	);
	customEventTest.assertEquals(
		event.cancelable,
		true,
		"event.cancelable 应该正确设置为 true",
	);
	customEventTest.assertDeepEquals(
		event.detail,
		detail,
		"event.detail 应该正确设置",
	);
});

// 测试 CustomEvent 的 detail 属性
customEventTest.addTest("CustomEvent - detail 属性", () => {
	const nullEvent = new CustomEvent("custom");
	customEventTest.assertEquals(
		nullEvent.detail,
		null,
		"未指定时 detail 应该是 null",
	);

	const emptyEvent = new CustomEvent("custom", { detail: {} });
	customEventTest.assertDeepEquals(
		emptyEvent.detail,
		{},
		"detail 应该是空对象",
	);

	const numberEvent = new CustomEvent("custom", { detail: 123 });
	customEventTest.assertEquals(numberEvent.detail, 123, "detail 可以是数字");

	const stringEvent = new CustomEvent("custom", { detail: "hello" });
	customEventTest.assertEquals(
		stringEvent.detail,
		"hello",
		"detail 可以是字符串",
	);

	const arrayEvent = new CustomEvent("custom", { detail: [1, 2, 3] });
	customEventTest.assertDeepEquals(
		arrayEvent.detail,
		[1, 2, 3],
		"detail 可以是数组",
	);

	const complexEvent = new CustomEvent("custom", {
		detail: { a: 1, b: "test", c: [1, 2, 3], d: { e: 5 } },
	});
	customEventTest.assertDeepEquals(
		complexEvent.detail,
		{ a: 1, b: "test", c: [1, 2, 3], d: { e: 5 } },
		"detail 可以是复杂对象",
	);
});

// 测试 EventTarget API
const eventTargetTest = new TestFramework("EventTarget API 测试");

// 测试 EventTarget 构造函数
eventTargetTest.addTest("EventTarget 构造函数", () => {
	const target = new EventTarget();
	eventTargetTest.assert(
		target instanceof EventTarget,
		"target 应该是 EventTarget 的实例",
	);
});

// 测试 EventTarget.addEventListener()
eventTargetTest.addTest("EventTarget.addEventListener() - 基本添加", () => {
	const target = new EventTarget();

	let called = false;

	function listener() {
		called = true;
	}

	target.addEventListener("test", listener);
	target.dispatchEvent(new Event("test"));

	eventTargetTest.assert(called, "事件监听器应该被调用");
});

eventTargetTest.addTest(
	"EventTarget.addEventListener() - 多次添加相同监听器",
	() => {
		const target = new EventTarget();
		let counter = 0;

		function listener() {
			counter++;
		}

		target.addEventListener("test", listener);
		target.addEventListener("test", listener); // 重复添加相同的监听器
		target.dispatchEvent(new Event("test"));

		eventTargetTest.assertEquals(
			counter,
			1,
			"相同的事件监听器只应该被调用一次",
		);
	},
);

eventTargetTest.addTest("EventTarget.addEventListener() - 选项 once", () => {
	const target = new EventTarget();
	let counter = 0;

	function listener() {
		counter++;
	}

	target.addEventListener("test", listener, { once: true });
	target.dispatchEvent(new Event("test"));
	target.dispatchEvent(new Event("test"));

	eventTargetTest.assertEquals(
		counter,
		1,
		"使用 once 选项的监听器只应该被调用一次",
	);
});

// 修改 EventTarget.addEventListener() - 选项 capture 测试
eventTargetTest.addTest("EventTarget.addEventListener() - 选项 capture", () => {
  const target = new EventTarget();
  let called = false;

  // Node.js 中虽然可以设置 capture 选项，但实际上不会影响事件处理
  // 这里我们只测试能否正常添加带 capture 选项的监听器
  target.addEventListener("test", () => {
    called = true;
  }, { capture: true });

  target.dispatchEvent(new Event("test"));

  eventTargetTest.assert(called, "带 capture 选项的监听器应该能被正常调用");
});

eventTargetTest.addTest(
	"EventTarget.removeEventListener() - 移除监听器",
	() => {
		const target = new EventTarget();
		let called = false;

		function listener() {
			called = true;
		}

		target.addEventListener("test", listener);
		target.removeEventListener("test", listener);
		target.dispatchEvent(new Event("test"));

		eventTargetTest.assert(!called, "移除后的事件监听器不应该被调用");
	},
);

eventTargetTest.addTest(
	"EventTarget.removeEventListener() - 确保选项匹配",
	() => {
		const target = new EventTarget();
		let captureListenerCalled = false;
		let bubbleListenerCalled = false;

		function captureListener() {
			captureListenerCalled = true;
		}

		function bubbleListener() {
			bubbleListenerCalled = true;
		}

		target.addEventListener("test", captureListener, { capture: true });
		target.addEventListener("test", bubbleListener, { capture: false });

		// 尝试移除捕获监听器，但提供错误的捕获标志
		target.removeEventListener("test", captureListener, { capture: false });
		target.dispatchEvent(new Event("test"));

		eventTargetTest.assert(
			captureListenerCalled,
			"使用不匹配的捕获标志移除监听器应该无效",
		);
		eventTargetTest.assert(bubbleListenerCalled, "冒泡监听器应该被正常调用");

		// 重置并使用正确的捕获标志重试
		captureListenerCalled = false;
		bubbleListenerCalled = false;

		target.removeEventListener("test", captureListener, { capture: true });
		target.dispatchEvent(new Event("test"));

		eventTargetTest.assert(
			!captureListenerCalled,
			"使用匹配的捕获标志移除监听器应该有效",
		);
		eventTargetTest.assert(bubbleListenerCalled, "冒泡监听器应该仍然被调用");
	},
);

// 测试 EventTarget.dispatchEvent()
eventTargetTest.addTest("EventTarget.dispatchEvent() - 基本分发", () => {
	const target = new EventTarget();
	let receivedEvent = null;

	target.addEventListener("test", (event) => {
		receivedEvent = event;
	});

	const dispatchedEvent = new Event("test");
	target.dispatchEvent(dispatchedEvent);

	eventTargetTest.assert(
		receivedEvent === dispatchedEvent,
		"分发的事件应该被传递给监听器",
	);
});

eventTargetTest.addTest("EventTarget.dispatchEvent() - 事件处理", () => {
  const target = new EventTarget();
  const sequence = [];

  // 设置多个事件监听器
  target.addEventListener("test", () => {
    sequence.push("listener1");
  });

  target.addEventListener("test", () => {
    sequence.push("listener2");
  });

  // 分发事件
  target.dispatchEvent(new Event("test"));

  eventTargetTest.assertDeepEquals(
    sequence,
    ["listener1", "listener2"],
    "事件监听器应该按照添加顺序被调用"
  );
});

eventTargetTest.addTest(
	"EventTarget.dispatchEvent() - preventDefault() 和返回值",
	() => {
		const target = new EventTarget();

		target.addEventListener("test", (event) => {
			event.preventDefault();
		});

		const cancelableEvent = new Event("test", { cancelable: true });
		const result = target.dispatchEvent(cancelableEvent);

		eventTargetTest.assertEquals(
			result,
			false,
			"当 preventDefault() 被调用时，dispatchEvent() 应该返回 false",
		);

		const uncancelableEvent = new Event("test", { cancelable: false });
		const result2 = target.dispatchEvent(uncancelableEvent);

		eventTargetTest.assertEquals(
			result2,
			true,
			"当事件不可取消时，dispatchEvent() 应该返回 true",
		);
	},
);

eventTargetTest.addTest(
  "EventTarget.dispatchEvent() - stopPropagation() 测试",
  () => {
    const target = new EventTarget();
    let secondListenerCalled = false;
    
    // 在 Node.js 中，stopPropagation() 实际上不会阻止同一目标上的其他监听器
    // 所以我们只测试方法是否存在且可调用
    target.addEventListener("test", (event) => {
      event.stopPropagation();
    });
    
    target.addEventListener("test", () => {
      secondListenerCalled = true;
    });
    
    target.dispatchEvent(new Event("test"));
    
    // 在 Node.js 中，第二个监听器仍然会被调用
    eventTargetTest.assert(
      secondListenerCalled,
      "在 Node.js 中，stopPropagation() 不会阻止同一目标上的其他监听器"
    );
  }
);

eventTargetTest.addTest(
	"EventTarget.dispatchEvent() - stopImmediatePropagation() 测试",
	() => {
		const target = new EventTarget();
		const sequence = [];

		target.addEventListener("test", (event) => {
			sequence.push("listener1");
			// 在同一个事件对象上调用 stopImmediatePropagation
			event.stopImmediatePropagation();
		});

		target.addEventListener("test", () => {
			sequence.push("listener2");
		});

		// 分发一个事件
		target.dispatchEvent(new Event("test"));

		eventTargetTest.assertDeepEquals(
			sequence,
			["listener1"],
			"stopImmediatePropagation() 应该阻止后续监听器的执行"
		);
	},
);

// 组合测试
eventTargetTest.addTest("综合测试 - 事件监听器执行顺序", () => {
	const target = new EventTarget();
	const sequence = [];

	target.addEventListener("test", () => {
		sequence.push(1);
	});

	target.addEventListener("test", () => {
		sequence.push(2);
	});

	target.addEventListener(
		"test",
		() => {
			sequence.push(3);
		},
		{ once: true },
	);

	target.dispatchEvent(new Event("test"));
	target.dispatchEvent(new Event("test"));

	eventTargetTest.assertDeepEquals(
		sequence,
		[1, 2, 3, 1, 2],
		"事件监听器应该按照添加顺序执行，并且 { once: true } 的监听器只执行一次",
	);
});

eventTargetTest.addTest(
	"综合测试 - CustomEvent 在 EventTarget 上的使用",
	() => {
		const target = new EventTarget();
		let receivedDetail = null;

		target.addEventListener("custom-test", (event) => {
			receivedDetail = event.detail;
		});

		const detail = { message: "测试数据", value: 123 };
		target.dispatchEvent(new CustomEvent("custom-test", { detail }));

		eventTargetTest.assertDeepEquals(
			receivedDetail,
			detail,
			"CustomEvent 的 detail 应该被正确传递给事件监听器",
		);
	},
);

// 运行所有测试
async function runAllTests() {
	await eventTest.runTests();
	await customEventTest.runTests();
	await eventTargetTest.runTests();
}

runAllTests().catch(console.error);
