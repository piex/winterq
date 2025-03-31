class TestFramework {
	constructor(name) {
		this.name = name;
		this.tests = [];
		this.passedTests = 0;
		this.failedTests = 0;
	}

	addTest(name, testFn) {
		this.tests.push({ name, testFn });
		return this;
	}

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

// URL 构造函数测试
const urlConstructorTest = new TestFramework("URL 构造函数测试");

urlConstructorTest.addTest("URL 构造函数 - 基本用法", () => {
    const url = new URL('https://www.example.com');
    urlConstructorTest.assertEquals(url.protocol, 'https:', 'protocol 应该正确设置');
    urlConstructorTest.assertEquals(url.hostname, 'www.example.com', 'hostname 应该正确设置');
});

urlConstructorTest.addTest("URL 构造函数 - 完整 URL", () => {
    const url = new URL('https://user:pass@www.example.com:8080/path?query=value#hash');
    urlConstructorTest.assertEquals(url.protocol, 'https:', 'protocol 应该正确');
    urlConstructorTest.assertEquals(url.username, 'user', 'username 应该正确');
    urlConstructorTest.assertEquals(url.password, 'pass', 'password 应该正确');
    urlConstructorTest.assertEquals(url.hostname, 'www.example.com', 'hostname 应该正确');
    urlConstructorTest.assertEquals(url.port, '8080', 'port 应该正确');
    urlConstructorTest.assertEquals(url.pathname, '/path', 'pathname 应该正确');
    urlConstructorTest.assertEquals(url.search, '?query=value', 'search 应该正确');
    urlConstructorTest.assertEquals(url.hash, '#hash', 'hash 应该正确');
});

urlConstructorTest.addTest("URL 构造函数 - 相对 URL", () => {
    const url = new URL('/path', 'https://www.example.com');
    urlConstructorTest.assertEquals(url.href, 'https://www.example.com/path', '相对 URL 应该正确解析');
});

urlConstructorTest.addTest("URL 构造函数 - 特殊字符处理", () => {
    const url = new URL('https://example.com/path with spaces/');
    urlConstructorTest.assertEquals(
        url.pathname, 
        '/path%20with%20spaces/', 
        '特殊字符应该被正确编码'
    );
});

urlConstructorTest.addTest("URL 构造函数 - 无协议", () => {
    try {
        new URL('www.example.com');
        urlConstructorTest.assert(false, "应该抛出错误");
    } catch (e) {
        urlConstructorTest.assert(e instanceof TypeError, "应该抛出 TypeError");
    }
});

urlConstructorTest.addTest("URL 构造函数 - 无效协议", () => {
    try {
        new URL('invalid://example.com');
        urlConstructorTest.assert(false, "应该抛出错误");
    } catch (e) {
        urlConstructorTest.assert(e instanceof Error, "应该抛出 TypeError");
    }
});

urlConstructorTest.addTest("URL 构造函数 - 空字符串", () => {
    try {
        new URL('');
        urlConstructorTest.assert(false, "应该抛出错误");
    } catch (e) {
        urlConstructorTest.assert(e instanceof TypeError, "应该抛出 TypeError");
    }
});

// URL 属性测试
const urlPropertiesTest = new TestFramework("URL 属性测试");

urlPropertiesTest.addTest("URL 属性 - 设置和获取", () => {
    const url = new URL('https://www.example.com/path');
    
    url.protocol = 'http:';
    urlPropertiesTest.assertEquals(url.protocol, 'http:', 'protocol 可以被修改');
    
    url.hostname = 'test.com';
    urlPropertiesTest.assertEquals(url.hostname, 'test.com', 'hostname 可以被修改');
    
    url.pathname = '/new-path';
    urlPropertiesTest.assertEquals(url.pathname, '/new-path', 'pathname 可以被修改');
    
    url.port = '8080';
    urlPropertiesTest.assertEquals(url.port, '8080', 'port 可以被修改');
});

urlPropertiesTest.addTest("URL 属性 - 修改哈希", () => {
    const url = new URL('https://example.com/path');
    url.hash = '#new-hash';
    urlPropertiesTest.assertEquals(url.hash, '#new-hash', 'hash 可以被修改');
    urlPropertiesTest.assertEquals(url.href, 'https://example.com/path#new-hash', 'href 应该自动更新');
});

// URLSearchParams 构造函数测试
const urlSearchParamsConstructorTest = new TestFramework("URLSearchParams 构造函数测试");

urlSearchParamsConstructorTest.addTest("URLSearchParams 构造函数 - 字符串", () => {
    const params = new URLSearchParams('key1=value1&key2=value2');
    urlSearchParamsConstructorTest.assertEquals(params.get('key1'), 'value1', '从字符串构造正确');
    urlSearchParamsConstructorTest.assertEquals(params.get('key2'), 'value2', '从字符串构造正确');
});

urlSearchParamsConstructorTest.addTest("URLSearchParams 构造函数 - 对象", () => {
    const params = new URLSearchParams({key1: 'value1', key2: 'value2'});
    urlSearchParamsConstructorTest.assertEquals(params.get('key1'), 'value1', '从对象构造正确');
    urlSearchParamsConstructorTest.assertEquals(params.get('key2'), 'value2', '从对象构造正确');
});

urlSearchParamsConstructorTest.addTest("URLSearchParams 构造函数 - 数组", () => {
    const params = new URLSearchParams([
        ['key1', 'value1'],
        ['key2', 'value2']
    ]);
    urlSearchParamsConstructorTest.assertEquals(params.get('key1'), 'value1', '从数组构造正确');
    urlSearchParamsConstructorTest.assertEquals(params.get('key2'), 'value2', '从数组构造正确');
});

// URLSearchParams 方法测试
const urlSearchParamsMethodsTest = new TestFramework("URLSearchParams 方法测试");

urlSearchParamsMethodsTest.addTest("URLSearchParams - append 和 get", () => {
    const params = new URLSearchParams();
    params.append('key', 'value1');
    params.append('key', 'value2');
    
    urlSearchParamsMethodsTest.assertEquals(params.get('key'), 'value1', 'get 返回第一个值');
    urlSearchParamsMethodsTest.assertDeepEquals(
        params.getAll('key'), 
        ['value1', 'value2'], 
        'getAll 返回所有值'
    );
});

urlSearchParamsMethodsTest.addTest("URLSearchParams - delete 和 has", () => {
    const params = new URLSearchParams('key1=value1&key2=value2');
    urlSearchParamsMethodsTest.assert(params.has('key1'), 'has 应该返回 true');
    urlSearchParamsMethodsTest.assert(!params.has('key3'), 'has 应该返回 false');
    
    params.delete('key1');
    urlSearchParamsMethodsTest.assert(!params.has('key1'), 'delete 应该移除指定键');
});

urlSearchParamsMethodsTest.addTest("URLSearchParams - set 方法", () => {
    const params = new URLSearchParams('key=oldvalue');
    params.set('key', 'newvalue');
    urlSearchParamsMethodsTest.assertEquals(params.get('key'), 'newvalue', 'set 应该更新值');
});

urlSearchParamsMethodsTest.addTest("URLSearchParams - toString 方法", () => {
    const params = new URLSearchParams({
        key1: 'value1', 
        key2: 'value 2'
    });
    urlSearchParamsMethodsTest.assertEquals(
        params.toString(), 
        'key1=value1&key2=value+2', 
        'toString 应该正确编码查询参数'
    );
});

urlSearchParamsMethodsTest.addTest("URLSearchParams - entries 方法", () => {
    const params = new URLSearchParams('key1=value1&key2=value2');
    const entries = Array.from(params.entries());
    urlSearchParamsMethodsTest.assertDeepEquals(
        entries,
        [['key1', 'value1'], ['key2', 'value2']],
        'entries 应该返回正确的键值对'
    );
});

// URL 和 URLSearchParams 综合测试
const urlIntegrationTest = new TestFramework("URL 和 URLSearchParams 综合测试");

urlIntegrationTest.addTest("URL 和 URLSearchParams 集成", () => {
    const url = new URL('https://example.com/search');
    const params = new URLSearchParams();
    
    params.append('q', 'test query');
    params.append('category', 'books');
    
    url.search = params.toString();
    
    urlIntegrationTest.assertEquals(
        url.href, 
        'https://example.com/search?q=test+query&category=books', 
        'URL 和 URLSearchParams 应该可以无缝集成'
    );
});

// 运行所有测试
async function runAllTests() {
    await urlConstructorTest.runTests();
    await urlPropertiesTest.runTests();
    await urlSearchParamsConstructorTest.runTests();
    await urlSearchParamsMethodsTest.runTests();
    await urlIntegrationTest.runTests();
}

runAllTests().catch(console.error);