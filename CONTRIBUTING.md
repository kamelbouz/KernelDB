\# Contribuer à KernelDB



Merci de votre intérêt pour \*\*KernelDB\*\* ! Ce guide explique comment contribuer

au projet, que ce soit par un rapport de bug, une suggestion, une amélioration

de la documentation ou du code.



KernelDB est un moteur relationnel embarqué en C++17, sans dépendance externe.

Le projet est en développement actif ; il n'est pas encore prêt pour un usage

en production. Voir \[README.md](README.md), \[VISION.md](VISION.md) et

\[PROJET.md](PROJET.md) pour le contexte.



\## Code de conduite



Toute participation est soumise au \[Code de conduite](CODE\_OF\_CONDUCT.md).

En participant, vous vous engagez à le respecter. Merci de signaler tout

comportement inacceptable à `conduct@kerneldb.dev`.



\## Comment contribuer



\### 1. Signaler un bug



Avant d'ouvrir une issue, vérifiez qu'elle n'existe pas déjà dans

\[les issues](https://github.com/kamelbouz/KernelDB/issues).



Si le bug est nouveau, ouvrez une issue avec :



\- \*\*Titre clair\*\* : « Crash lors de INSERT avec VARCHAR NULL »

\- \*\*Description\*\* : ce que vous attendiez vs ce qui se passe

\- \*\*Reproductible\*\* : commandes SQL exactes, ou code C++ minimal

\- \*\*Environnement\*\* : OS, compilateur, version de KernelDB (`git rev-parse HEAD`)

\- \*\*Logs\*\* : sortie complète, y compris les messages d'erreur



Un bug \*\*sans reproductible\*\* ne pourra pas être corrigé. Prenez le temps de

réduire votre cas à un minimum.



\### 2. Proposer une fonctionnalité



Ouvrez d'abord une \*\*discussion\*\* (pas une issue) pour expliquer :



\- \*\*Le besoin\*\* : quel problème ça résout

\- \*\*La proposition\*\* : comment vous voyez la chose

\- \*\*Les alternatives\*\* : ce que vous avez considéré



Attendez un retour avant de coder. Une fonctionnalité qui ne correspond pas

à la vision du projet ne sera pas acceptée, même bien codée.



\### 3. Améliorer la documentation



Les docs sont dans `README.md`, `docs/`, et les fichiers `.md` à la racine.

Une PR de doc est \*\*toujours bienvenue\*\* — c'est le meilleur premier pas.



\### 4. Contribuer du code



\#### Fork et clone



```bash

\# Sur GitHub : cliquer sur "Fork"

git clone https://github.com/<votre-compte>/KernelDB.git

cd KernelDB

git remote add upstream https://github.com/kamelbouz/KernelDB.git

