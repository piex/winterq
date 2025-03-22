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
    console.log('====================================');

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

    console.log('====================================');
    console.log(`测试结果: ${this.passedTests} 通过, ${this.failedTests} 失败\n`);
  }

  // 断言函数
  assert(condition, message) {
    if (!condition) {
      throw new Error(message || '断言失败');
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
      throw new Error(message || `期望值 ${expectedJson}, 实际值 ${actualJson}`);
    }
  }
}

// 测试 Headers API
const headersTest = new TestFramework('Headers API 测试');

// 测试 Headers 构造函数
headersTest.addTest('Headers 构造函数 - 空构造函数', () => {
  const headers = new Headers();
  headersTest.assert(headers instanceof Headers, 'headers 应该是 Headers 的实例');
});

headersTest.addTest('Headers 构造函数 - 对象参数', () => {
  const headers = new Headers({
    'Content-Type': 'application/json',
    'X-Custom-Header': 'test'
  });
  
  headersTest.assertEquals(headers.get('content-type'), 'application/json', 'headers 应该包含 content-type');
  headersTest.assertEquals(headers.get('x-custom-header'), 'test', 'headers 应该包含 x-custom-header');
});

headersTest.addTest('Headers 构造函数 - 数组参数', () => {
  const headers = new Headers([
    ['Content-Type', 'application/json'],
    ['X-Custom', 'test']
  ]);

  headersTest.assertEquals(headers.get('Content-Type'), 'application/json', 'headers 应该正确解析数组形式的 headers');
  headersTest.assertEquals(headers.get('X-Custom'), 'test', 'headers 应该正确解析数组形式的 headers');
});


// 测试 Headers.append()
headersTest.addTest('Headers.append() - 添加新header', () => {
  const headers = new Headers();
  headers.append('Content-Type', 'application/json');
  
  headersTest.assertEquals(headers.get('Content-Type'), 'application/json', 'headers 应该包含被添加的 header');
});

headersTest.addTest('Headers.append() - 添加重复header', () => {
  const headers = new Headers();
  headers.append('Accept', 'application/json');
  headers.append('Accept', 'text/plain');
  
  headersTest.assertEquals(headers.get('Accept'), 'application/json, text/plain', 'headers 应该合并重复的 header 值');
});

// 测试 Headers.set()
headersTest.addTest('Headers.set() - 设置新header', () => {
  const headers = new Headers();
  headers.set('Content-Type', 'application/json');
  
  headersTest.assertEquals(headers.get('Content-Type'), 'application/json', 'headers 应该包含被设置的 header');
});

headersTest.addTest('Headers.set() - 覆盖已存在的header', () => {
  const headers = new Headers();
  headers.append('Accept', 'application/json');
  headers.set('Accept', 'text/plain');
  
  headersTest.assertEquals(headers.get('Accept'), 'text/plain', 'headers 应该覆盖已存在的 header');
});

// 测试 Headers.has()
headersTest.addTest('Headers.has() - 检查存在的header', () => {
  const headers = new Headers();
  headers.set('Content-Type', 'application/json');
  
  headersTest.assert(headers.has('Content-Type'), 'headers.has() 应该返回 true');
  headersTest.assert(headers.has('content-type'), 'headers.has() 应该不区分大小写');
});

headersTest.addTest('Headers.has() - 检查不存在的header', () => {
  const headers = new Headers();
  
  headersTest.assert(!headers.has('X-Custom'), 'headers.has() 应该返回 false');
});

// 测试大小写不敏感性
headersTest.addTest('Headers - 大小写不敏感', () => {
  const headers = new Headers();
  headers.set('Content-Type', 'application/json');
  
  headersTest.assertEquals(headers.get('content-type'), 'application/json', '获取 header 应该不区分大小写');
  headersTest.assertEquals(headers.get('CONTENT-TYPE'), 'application/json', '获取 header 应该不区分大小写');
});

// 测试 Headers.delete()
headersTest.addTest('Headers.delete() - 删除存在的header', () => {
  const headers = new Headers();
  headers.set('Content-Type', 'application/json');
  headers.delete('Content-Type');
  
  headersTest.assert(!headers.has('Content-Type'), 'headers 应该删除指定的 header');
});

headersTest.addTest('Headers.delete() - 大小写不敏感', () => {
  const headers = new Headers();
  headers.set('X-Test', 'value');

  headers.delete('x-test');

  headersTest.assert(!headers.has('X-Test'), 'delete() 应该大小写不敏感');
});

// 测试 Headers.forEach()
headersTest.addTest('Headers.forEach() - 遍历所有headers', () => {
  const headers = new Headers({
    'Content-Type': 'application/json',
    'X-Custom': 'test'
  });
  
  const result = {};
  headers.forEach((value, name) => {
    result[name] = value;
  });
  
  headersTest.assertDeepEquals(
    result, 
    { 'Content-Type': 'application/json', 'X-Custom': 'test' },
    'headers.forEach() 应该遍历所有 headers'
  );
});

headersTest.addTest('Headers.entries() - 遍历所有 headers', () => {
  const headers = new Headers({ 'Content-Type': 'application/json', 'X-Custom': 'test' });
  const entries = [...headers.entries()];
  headersTest.assertDeepEquals(entries, [['Content-Type', 'application/json'], ['X-Custom', 'test']], 'entries() 应该返回所有 headers');
});

headersTest.addTest('Headers.keys() - 遍历所有 keys', () => {
  const headers = new Headers({ 'Content-Type': 'application/json', 'X-Custom': 'test' });
  const keys = [...headers.keys()];

  headersTest.assertDeepEquals(keys, ['Content-Type', 'X-Custom'], 'keys() 应该返回所有 header 名称');
});

headersTest.addTest('Headers.values() - 遍历所有 values', () => {
  const headers = new Headers({ 'Content-Type': 'application/json', 'X-Custom': 'test' });
  const values = [...headers.values()];

  headersTest.assertDeepEquals(values, ['application/json', 'test'], 'values() 应该返回所有 header 值');
});

headersTest.addTest('Headers.set() - 设置无效 header 名称', () => {
  const headers = new Headers();

  try {
    headers.set('', 'test');
    headersTest.assert(false, '空 header 名称应抛出异常');
  } catch (error) {
    headersTest.assert(true);
  }

  try {
    headers.set('Invalid Header', 'test');
    headersTest.assert(false, '非法 header 名称应抛出异常');
  } catch (error) {
    headersTest.assert(true);
  }
});

headersTest.addTest('Headers.set() - 处理 null 和 undefined', () => {
  const headers = new Headers();

  headers.set('X-Test-Null', null);
  headers.set('X-Test-Undefined', undefined);

  headersTest.assertEquals(headers.get('X-Test-Null'), 'null', 'null 应该转换为字符串');
  headersTest.assertEquals(headers.get('X-Test-Undefined'), 'undefined', 'undefined 应该转换为字符串');
});

// 运行所有测试
headersTest.runTests().catch(console.error);
