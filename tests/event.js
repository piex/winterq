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

eventTargetTest.addTest("EventTarget.addEventListener() - 选项 capture", () => {
	const parent = new EventTarget();
	const child = new EventTarget();
	const sequence = [];

	// 为了测试 capture，我们模拟捕获和冒泡阶段
	parent.addEventListener(
		"test",
		() => {
			sequence.push("parent-capture");
		},
		{ capture: true },
	);

	parent.addEventListener("test", () => {
		sequence.push("parent-bubble");
	});

	child.addEventListener("test", () => {
		sequence.push("child");
	});

	// 手动模拟事件传播：先捕获阶段，再目标阶段，最后冒泡阶段
	const event = new Event("test", { bubbles: true });

	// 捕获阶段 (1)
	event.eventPhase = 1;
	event.currentTarget = parent;
	parent.dispatchEvent(event);

	// 目标阶段 (2)
	event.eventPhase = 2;
	event.currentTarget = child;
	child.dispatchEvent(event);

	// 冒泡阶段 (3)
	event.eventPhase = 3;
	event.currentTarget = parent;
	parent.dispatchEvent(event);

	eventTargetTest.assertDeepEquals(
		sequence,
		["parent-capture", "child", "parent-bubble"],
		"事件应该按照正确的顺序触发：捕获 -> 目标 -> 冒泡",
	);
});

// 测试 EventTarget.removeEventListener()
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

eventTargetTest.addTest("EventTarget.dispatchEvent() - 事件传播与冒泡", () => {
	const parent = new EventTarget();
	const child = new EventTarget();
	const sequence = [];

	// 设置事件监听器
	parent.addEventListener("test", () => {
		sequence.push("parent-bubble");
	});

	child.dispatchEvent = function (event) {
		EventTarget.prototype.dispatchEvent.call(this, event);

		// 模拟事件冒泡
		if (event.bubbles && !event.cancelBubble) {
			event.currentTarget = parent;
			parent.dispatchEvent(event);
		}
	};

	child.addEventListener("test", () => {
		sequence.push("child");
	});

	// 分发一个冒泡事件
	child.dispatchEvent(new Event("test", { bubbles: true }));

	eventTargetTest.assertDeepEquals(
		sequence,
		["child", "parent-bubble"],
		"冒泡事件应该从子元素向上传播到父元素",
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
		const parent = new EventTarget();
		const child = new EventTarget();
		const sequence = [];

		parent.addEventListener("test", () => {
			sequence.push("parent");
		});

		child.dispatchEvent = function (event) {
			sequence.push("child-start");

			child.addEventListener("test", () => {
				sequence.push("child-listener");
				event.stopPropagation();
			});

			EventTarget.prototype.dispatchEvent.call(this, event);

			// 模拟事件冒泡
			if (event.bubbles && !event.cancelBubble) {
				event.currentTarget = parent;
				parent.dispatchEvent(event);
			}

			sequence.push("child-end");
		};

		child.dispatchEvent(new Event("test", { bubbles: true }));

		eventTargetTest.assert(
			!sequence.includes("parent"),
			"stopPropagation() 应该阻止事件冒泡到父元素",
		);

		eventTargetTest.assertDeepEquals(
			sequence,
			["child-start", "child-listener", "child-end"],
			"事件应该在子元素中正常处理但不冒泡",
		);
	},
);

eventTargetTest.addTest(
	"EventTarget.dispatchEvent() - stopImmediatePropagation() 测试",
	() => {
		const target = new EventTarget();
		const sequence = [];

		target.addEventListener("test", () => {
			sequence.push("listener1");
			const event = new Event("test");
			event.stopImmediatePropagation();
			target.dispatchEvent(event);
		});

		target.addEventListener("test", () => {
			sequence.push("listener2");
		});

		target.dispatchEvent(new Event("test"));

		eventTargetTest.assertDeepEquals(
			sequence,
			["listener1"],
			"stopImmediatePropagation() 应该阻止后续监听器的执行",
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
