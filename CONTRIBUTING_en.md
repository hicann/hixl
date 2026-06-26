# Contributions

Developers are welcome to experience and contribute to this project. Before contributing to the community, please refer to the [cann-community](https://gitcode.com/cann/community) to understand the code of conduct, sign the CLA, and learn about the contribution process of the source code repository. This repository describes the prerequisites for contributing to the CANN open-source project, including but not limited to:

1. How to submit a PR [Code Contribution Process: Integrating Code from Scratch](https://gitcode.com/cann/hixl/wiki/%E4%BB%A3%E7%A0%81%E8%B4%A1%E7%8C%AE%E6%B5%81%E7%A8%8B%EF%BC%9A%E4%BB%8E0%E5%BC%80%E5%A7%8B%E5%90%88%E5%85%A5%E4%BB%A3%E7%A0%81.md)
2. GitCode workflow
3. Pipeline trigger commands
4. Code review
5. Other precautions
   For details, see [cann-community](https://gitcode.com/cann/community).

In addition, developers need to pay close attention to the following points when preparing local code and submitting PRs:

1. When submitting a PR, fill in the PR template carefully, providing information such as business background, purpose, solution, etc.
2. Before committing code using Git, refer to [pre-commit Usage Guide](./docs/en/contributions/precommit_guide.md)​ to ensure that your submissions are compliant and efficient.
3. If your changes are not simple bug fixes, but involve adding new features, interfaces, configuration parameters, or modifying code flows, please discuss the design through an issue first to avoid rejection. If you are not sure whether the modification can be classified as a simple bug fix, you can submit an issue for discussion. 
4. When submitting a PR, ensure that your code complies with the project's coding standards. For details, see [Google Style Guides](https://google.github.io/styleguide/), including but not limited to:
  - Code formatting
  - Comment conventions
  - Variable naming conventions
  - Function naming conventions
  - Class naming conventions
  - API naming conventions
  - Configuration parameter naming conventions
  - Code flow conventions
5. If multiple invalid commits exist when submitting a PR, you are advised to perform a rebase​ beforehand to combine them into one commit, keeping the code clean and readable. For details, see [git rebase](https://git-scm.com/docs/git-rebase). Also, the commit message must conform to project code conventions, clearly describing the intent and content of the change in the format: `<type>: <short description>`.  Example:

| Type| Description                   | Example                    |
|------|-----------------------|------------------------|
| feat     | New feature                  | [feat]: Added user registration.       |
| bugfix   | Bug fix               | [bugfix]: Fixed the issue of expired login.   |
| docs     | File update                 | [docs]: Updated the API usage guide.   |
| style    | Code format adjustment (without affecting the logic).  | [style]: Adjusted the code indentation.       |
| refactor | Rebuild (not involving new feature or bug fix)     | [refactor]: Optimized the user service class structure. |
| perf     | Performance optimization                 | [perf]: Reduced database queries.     |
| test     | Test                  | [test]: Added the login unit test.    |
| chore    | Build/Tool chain change            | [chore]: Updated the webpack configuration.|
| ci       | CI configuration              | [ci]: Added the automated testing process.       |

Developer contribution scenarios include:

- Fix bugs

  If you find some bugs in this project and want to fix them, feel free to create an issue for feedback and tracking.

  You can create a `Bug-Report` issue to describe the bug according to [Submit Issue/Handle Issue Task](https://gitcode.com/cann/community#Submitting-an-Issue/Handling-an-Issue-Task). Then enter `/assign` or `/assign @yourself` in the comment box to assign the issue to yourself for handling.

- Contribute new features

  If you find that some functions are missing in this project and want to add them, feel free to create an issue for feedback and tracking.

  You can create a `Requirement` issue to describe the new function and provide your design solution according to [Submit Issue/Handle Issue Task](https://gitcode.com/cann/community#Submitting-an-Issue/Handling-an-Issue-Task).
  Then enter `/assign` or `/assign @yourself` in the comment box to assign the issue to yourself for handling.

- Report document error

  If you find any errors in documents of this project, feel free to create an issue for feedback and correction.

  You can create a `Documentation` issue to point out the problem in the document according to [Submit Issue/Handle Issue Task](https://gitcode.com/cann/community#Submitting-an-Issue/Handling-an-Issue-Task). Then enter `/assign` or `/assign @yourself` in the comment box to assign the Issue to yourself for handling.

- Help resolve others' issues

  If you have a suitable solution to a problem encountered by others in the community, feel free to comment on the issue to help solve their problems, working together to improve overall usability.

  If the issue requires code modification, you can enter `/assign` or `/assign @yourself` in the comment box to assign the issue to yourself for assisted handling.
